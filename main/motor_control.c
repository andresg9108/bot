#include "motor_control.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* Cableado del L298N. ENA/ENB reciben PWM sin sus jumpers.
 * IN1/IN2 controlan el sentido; ENA/ENB regulan la potencia de cada lado.
 * L298N wiring. ENA/ENB receive PWM with their jumpers removed.
 * IN1/IN2 control direction; ENA/ENB regulate power on each side. */
#define LEFT_IN1 GPIO_NUM_32
#define LEFT_IN2 GPIO_NUM_33
#define RIGHT_IN1 GPIO_NUM_25
#define RIGHT_IN2 GPIO_NUM_26
#define LEFT_ENABLE GPIO_NUM_22
#define RIGHT_ENABLE GPIO_NUM_23
/* La tarea actualiza el movimiento cada 20 ms y comunica el estado cada 100 ms.
 * The task updates motion every 20 ms and reports status every 100 ms. */
#define CONTROL_PERIOD_MS 20u
#define STATUS_PERIOD_MS 100u
/* Ambos canales PWM comparten el temporizador configurado al inicializar.
 * Both PWM channels share the timer configured during initialization. */
#define PWM_MODE LEDC_LOW_SPEED_MODE
#define LEFT_CHANNEL LEDC_CHANNEL_0
#define RIGHT_CHANNEL LEDC_CHANNEL_1

static const char *TAG = "motors";
/* Cola de un elemento: conserva solo el comando más reciente.
 * Single-item queue: keeps only the latest command. */
static QueueHandle_t commands;
/* Función de la aplicación que recibe el estado desde la tarea de motores.
 * Application function that receives status from the motor task. */
static motor_status_callback_t status_callback;

typedef struct {
    uint8_t buttons;         /* Máscara binaria de los botones pulsados.
                             * Bitmask of pressed buttons. */
    TickType_t received_at;  /* Instante de recepción para detectar comandos vencidos.
                             * Reception time used to detect expired commands. */
} motor_command_t;

/* Conserva el primer error sin impedir que se intenten las salidas restantes.
 * Keeps the first error while still attempting the remaining outputs. */
static void record_error(esp_err_t *result, esp_err_t error)
{
    if (*result == ESP_OK) *result = error;
}

static esp_err_t outputs_off(void)
{
    /* Deshabilita el puente antes de cambiar el sentido.
     * Detiene los PWM con salida baja y lleva las cuatro entradas de sentido a 0.
     * Disables the bridge before changing direction.
     * Stops PWM with low outputs and sets all four direction inputs to 0. */
    esp_err_t err = ESP_OK;
    record_error(&err, ledc_stop(PWM_MODE, LEFT_CHANNEL, 0));
    record_error(&err, ledc_stop(PWM_MODE, RIGHT_CHANNEL, 0));
    record_error(&err, gpio_set_level(LEFT_IN1, 0));
    record_error(&err, gpio_set_level(LEFT_IN2, 0));
    record_error(&err, gpio_set_level(RIGHT_IN1, 0));
    record_error(&err, gpio_set_level(RIGHT_IN2, 0));
    return err;
}

/* Traduce el movimiento calculado a GPIO/PWM y recuerda el sentido aplicado.
 * Translates calculated motion into GPIO/PWM and tracks the applied direction. */
static esp_err_t apply_motion(const motor_motion_t *motion, int *applied_direction)
{
    /* Durante la pausa de inversión, o sin potencia, mantiene el puente apagado.
     * Keeps the bridge off during the reversal pause or when there is no power. */
    int direction = motion->guard_ms ? 0 : motion->direction;
    if (!motion->left_pwm && !motion->right_pwm) direction = 0;
    if (direction != *applied_direction || !direction) {
        /* Apaga antes de modificar las entradas de sentido.
         * Turns outputs off before changing the direction inputs. */
        ESP_RETURN_ON_ERROR(outputs_off(), TAG, "No se pudieron deshabilitar los motores");
        *applied_direction = 0;
        if (!direction) return ESP_OK;
        /* Un sentido negativo activa IN1; uno positivo activa IN2, en ambos lados.
         * A negative direction activates IN1; a positive one activates IN2 on both sides. */
        ESP_RETURN_ON_ERROR(gpio_set_level(LEFT_IN1, direction < 0), TAG, "LEFT_IN1");
        ESP_RETURN_ON_ERROR(gpio_set_level(LEFT_IN2, direction > 0), TAG, "LEFT_IN2");
        ESP_RETURN_ON_ERROR(gpio_set_level(RIGHT_IN1, direction < 0), TAG, "RIGHT_IN1");
        ESP_RETURN_ON_ERROR(gpio_set_level(RIGHT_IN2, direction > 0), TAG, "RIGHT_IN2");
        *applied_direction = direction;
    }
    /* Aplica la potencia independiente de cada lado.
     * Applies independent power to each side. */
    ESP_RETURN_ON_ERROR(ledc_set_duty(PWM_MODE, LEFT_CHANNEL, motion->left_pwm), TAG, "PWM izquierdo");
    ESP_RETURN_ON_ERROR(ledc_update_duty(PWM_MODE, LEFT_CHANNEL), TAG, "Actualizar PWM izquierdo");
    ESP_RETURN_ON_ERROR(ledc_set_duty(PWM_MODE, RIGHT_CHANNEL, motion->right_pwm), TAG, "PWM derecho");
    return ledc_update_duty(PWM_MODE, RIGHT_CHANNEL);
}

/* Tarea FreeRTOS: procesa comandos, calcula el movimiento y actualiza las salidas.
 * FreeRTOS task: processes commands, calculates motion, and updates outputs. */
static void motor_task(void *arg)
{
    /* La tarea se crea sin argumentos adicionales.
     * The task is created without additional arguments. */
    (void)arg;
    /* Estado inicial sin movimiento; los tiempos se miden en ticks de FreeRTOS.
     * Initial state with no motion; times are measured in FreeRTOS ticks. */
    motor_motion_t motion = {0};
    TickType_t wake = xTaskGetTickCount();
    TickType_t previous = wake;
    TickType_t last_status = wake;
    int applied_direction = 0;
    bool fault = false;
    while (true) {
        /* Espera hasta el siguiente período para mantener una cadencia estable.
         * Waits until the next period to maintain a steady cadence. */
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
        TickType_t now = xTaskGetTickCount();
        /* Usa el tiempo real transcurrido para las rampas y temporizaciones.
         * Uses actual elapsed time for ramps and timing. */
        uint32_t elapsed = (now - previous) * portTICK_PERIOD_MS;
        previous = now;
        motor_command_t command = {0};
        /* Consulta la cola sin bloquear; puede no haber un comando nuevo.
         * Checks the queue without blocking; there may be no new command. */
        bool received = xQueueReceive(commands, &command, 0) == pdTRUE;
        /* Descarta comandos que ya vencieron en la cola.
         * Discards commands that have already expired in the queue. */
        if (received && now - command.received_at >= pdMS_TO_TICKS(MOTOR_COMMAND_TIMEOUT_MS)) {
            received = false;
        }
        /* La lógica de movimiento gestiona botones, rampa, inversión y timeout.
         * The motion logic handles buttons, ramping, reversal, and timeout. */
        motor_motion_update(&motion, received, command.buttons, elapsed);
        /* Incluye en la antigüedad el tiempo que el comando pasó en la cola.
         * Includes time spent in the queue in the command age. */
        if (received) motion.age_ms = (now - command.received_at) * portTICK_PERIOD_MS;
        /* Un fallo de salida bloquea el movimiento hasta reiniciar.
         * An output failure blocks motion until restart. */
        if (fault) {
            motion.buttons = 0;
            motion.direction = 0;
            motion.ramp_ms = 0;
            motion.left_pwm = 0;
            motion.right_pwm = 0;
            motion.guard_ms = 0;
        }
        esp_err_t err = apply_motion(&motion, &applied_direction);
        if (err != ESP_OK) {
            /* Registra solo el primer fallo e intenta apagar todas las salidas.
             * Logs only the first failure and attempts to turn all outputs off. */
            if (!fault) ESP_LOGE(TAG, "Fallo de salida; control deshabilitado hasta reiniciar: %s",
                                 esp_err_to_name(err));
            fault = true;
            outputs_off();
            motion.ramp_ms = 0;
            motion.left_pwm = 0;
            motion.right_pwm = 0;
        }
        /* Informa a BLE sin ejecutar GPIO desde su callback.
         * Reports to BLE without performing GPIO operations from its callback. */
        if (now - last_status >= pdMS_TO_TICKS(STATUS_PERIOD_MS)) {
            last_status = now;
            /* Paquete de cinco bytes: botones, base %, PWM izquierdo %, PWM
             * derecho % y bits de timeout, pausa de inversión o fallo de salida.
             * Five-byte packet: buttons, base %, left PWM %, right PWM %, and
             * flags for timeout, reversal pause, or output failure. */
            uint8_t status[MOTOR_STATUS_LENGTH] = {
                motion.buttons,
                motion.ramp_ms * 100 / MOTOR_ACCELERATION_MS,
                motion.left_pwm * 100 / MOTOR_PWM_MAX,
                motion.right_pwm * 100 / MOTOR_PWM_MAX,
                (motion.timed_out ? MOTOR_STATUS_TIMEOUT : 0) |
                (motion.guard_ms ? MOTOR_STATUS_PAUSED : 0) |
                (fault ? MOTOR_STATUS_FAULT : 0),
            };
            /* El callback debe consumir los datos aquí y no bloquear esta tarea.
             * The callback must consume the data here without blocking this task. */
            if (status_callback) status_callback(status);
        }
    }
}

/* Prepara el hardware apagado y crea la cola y la tarea de control.
 * Prepares the hardware in the off state and creates the control queue and task. */
esp_err_t motor_control_init(motor_status_callback_t callback)
{
    /* Una cola existente indica que el controlador ya fue inicializado.
     * An existing queue indicates that the controller is already initialized. */
    if (commands) return ESP_ERR_INVALID_STATE;
    /* Selecciona los seis GPIO como salidas, sin resistencias internas ni IRQ.
     * Configures all six GPIOs as outputs, without internal resistors or IRQs. */
    gpio_config_t pins = {
        .pin_bit_mask = (1ULL << LEFT_IN1) | (1ULL << LEFT_IN2) |
                        (1ULL << RIGHT_IN1) | (1ULL << RIGHT_IN2) |
                        (1ULL << LEFT_ENABLE) | (1ULL << RIGHT_ENABLE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pins), TAG, "Configurar GPIO");
    /* Fija primero todas las salidas a nivel bajo, incluidas ENA y ENB.
     * First sets all outputs low, including ENA and ENB. */
    const gpio_num_t outputs[] = {LEFT_IN1, LEFT_IN2, RIGHT_IN1, RIGHT_IN2, LEFT_ENABLE, RIGHT_ENABLE};
    for (unsigned i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
        ESP_RETURN_ON_ERROR(gpio_set_level(outputs[i], 0), TAG, "Inicializar GPIO");
    }
    /* PWM de 20 kHz y 10 bits para ambos motores: valores de duty de 0 a 1023.
     * 20 kHz, 10-bit PWM for both motors: duty values from 0 to 1023. */
    ledc_timer_config_t timer = {
        .speed_mode = PWM_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "Configurar temporizador PWM");
    /* Canal 0 en GPIO22 (izquierdo) y canal 1 en GPIO23 (derecho), ambos a 0.
     * Channel 0 on GPIO22 (left) and channel 1 on GPIO23 (right), both at 0. */
    ledc_channel_config_t channels[] = {
        {.gpio_num = LEFT_ENABLE, .speed_mode = PWM_MODE, .channel = LEFT_CHANNEL,
         .intr_type = LEDC_INTR_DISABLE, .timer_sel = LEDC_TIMER_0, .duty = 0, .hpoint = 0},
        {.gpio_num = RIGHT_ENABLE, .speed_mode = PWM_MODE, .channel = RIGHT_CHANNEL,
         .intr_type = LEDC_INTR_DISABLE, .timer_sel = LEDC_TIMER_0, .duty = 0, .hpoint = 0},
    };
    for (unsigned i = 0; i < sizeof(channels) / sizeof(channels[0]); ++i) {
        ESP_RETURN_ON_ERROR(ledc_channel_config(&channels[i]), TAG, "Configurar canal PWM");
    }
    /* Confirma el apagado antes de permitir la recepción de comandos.
     * Confirms outputs are off before allowing command reception. */
    ESP_RETURN_ON_ERROR(outputs_off(), TAG, "Parada inicial");
    /* Una sola posición: importa el estado más reciente.
     * A single slot: only the latest state matters. */
    commands = xQueueCreate(1, sizeof(motor_command_t));
    if (!commands) return ESP_ERR_NO_MEM;
    status_callback = callback;
    /* Crea la tarea con 4096 bytes de pila y prioridad 5.
     * Creates the task with a 4096-byte stack and priority 5. */
    if (xTaskCreate(motor_task, "motor_control", 4096, NULL, 5, NULL) != pdPASS) {
        /* Libera la cola si no se pudo iniciar la tarea.
         * Releases the queue if the task could not be started. */
        vQueueDelete(commands);
        commands = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* Recibe comandos sin bloquear; su aplicación al hardware ocurre en motor_task.
 * Receives commands without blocking; motor_task applies them to the hardware. */
esp_err_t motor_control_command(uint8_t buttons)
{
    if (!commands) return ESP_ERR_INVALID_STATE;
    /* Los botones inválidos se sustituyen por cero para solicitar una parada.
     * Invalid buttons are replaced with zero to request a stop. */
    bool valid = motor_buttons_valid(buttons);
    motor_command_t command = {
        .buttons = valid ? buttons : 0,
        .received_at = xTaskGetTickCount(),
    };
    /* Reemplaza cualquier comando pendiente: siempre interesa el más reciente.
     * Replaces any pending command: only the latest one matters. */
    if (xQueueOverwrite(commands, &command) != pdPASS) return ESP_FAIL;
    /* Informa al emisor si su comando original era inválido.
     * Informs the sender if the original command was invalid. */
    return valid ? ESP_OK : ESP_ERR_INVALID_ARG;
}

void motor_control_stop(void)
{
    /* Encola cero botones; la tarea de control ejecutará la parada.
     * Queues zero buttons; the control task will perform the stop. */
    (void)motor_control_command(0);
}
