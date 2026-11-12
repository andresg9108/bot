#include "agBLE.h"         /* Servidor BLE: comandos entrantes y estado saliente.
                           * BLE server: incoming commands and outgoing status. */
#include <stdatomic.h>     /* Acceso seguro a la bandera compartida entre tareas.
                           * Safe access to the flag shared between tasks. */
#include "motor_control.h" /* Inicialización, comandos y parada de los motores.
                           * Motor initialization, commands, and stopping. */
#include "esp_check.h"     /* Comprobación de errores con ESP_ERROR_CHECK.
                           * Error checking with ESP_ERROR_CHECK. */
#include "esp_log.h"       /* Mensajes de diagnóstico por el registro de ESP-IDF.
                           * Diagnostic messages through ESP-IDF logging. */
#include "nvs_flash.h"     /* Inicialización del almacenamiento no volátil.
                           * Non-volatile storage initialization. */

/* Pines del puente L298N, definidos y configurados en motor_control.c:
 * Motor izquierdo: IN1 = GPIO32, IN2 = GPIO33, ENA (PWM) = GPIO22.
 * Motor derecho:   IN3 = GPIO25, IN4 = GPIO26, ENB (PWM) = GPIO23.
 * Retirar los jumpers de ENA/ENB para usar las entradas PWM.
 * BLE utiliza la radio interna del ESP32 y no requiere GPIO externos.
 * L298N bridge pins, defined and configured in motor_control.c:
 * Left motor:  IN1 = GPIO32, IN2 = GPIO33, ENA (PWM) = GPIO22.
 * Right motor: IN3 = GPIO25, IN4 = GPIO26, ENB (PWM) = GPIO23.
 * Remove the ENA/ENB jumpers to use the PWM inputs.
 * BLE uses the ESP32 internal radio and requires no external GPIOs.
 */

/* Etiqueta que identifica los mensajes de este archivo.
 * Tag identifying messages from this file. */
static const char *TAG = "agbot";
/* Arranca en false; evita publicar desde la tarea de motores antes de iniciar BLE.
 * Starts as false; prevents the motor task from publishing before BLE starts. */
static atomic_bool ble_ready;

/* Callback de motores: recibe botones, potencia base, PWM izquierdo/derecho
 * y banderas de estado, en un paquete de MOTOR_STATUS_LENGTH bytes.
 * Motor callback: receives buttons, base power, left/right PWM, and status flags
 * in a MOTOR_STATUS_LENGTH-byte packet. */
static void on_motor_status(const uint8_t status[MOTOR_STATUS_LENGTH])
{
    /* La lectura atómica sincroniza esta tarea con la inicialización en app_main.
     * The atomic load synchronizes this task with initialization in app_main. */
    if (!atomic_load(&ble_ready)) return;
    /* Publica el estado actual para el cliente BLE.
     * Publishes the current status for the BLE client. */
    esp_err_t err = agble_server_publish(status, MOTOR_STATUS_LENGTH);
    if (err != ESP_OK) ESP_LOGE(TAG, "No se pudo publicar el estado: %s", esp_err_to_name(err));
}

/* Callback del servidor BLE: traduce sus eventos en acciones del controlador.
 * BLE server callback: translates its events into controller actions. */
static void on_ble_event(const agble_server_event_t *event, void *context)
{
    /* No se utiliza un contexto de usuario en esta aplicación.
     * This application does not use a user context. */
    (void)context;
    switch (event->type) {
    case AGBLE_SERVER_COMMAND: {
        /* El protocolo exige un byte binario de botones y datos accesibles.
         * The protocol requires one binary button byte and accessible data. */
        if (event->length != 1 || !event->data) {
            /* Un paquete mal formado solicita la parada en la tarea de motores.
             * A malformed packet requests a stop in the motor task. */
            motor_control_stop();
            ESP_LOGW(TAG, "Comando invalido: se requiere un byte binario de botones");
            break;
        }
        /* Encola los botones; la tarea de motores valida y calcula el movimiento.
         * Queues the buttons; the motor task validates and calculates motion. */
        esp_err_t err = motor_control_command(event->data[0]);
        if (err != ESP_OK) ESP_LOGW(TAG, "Comando rechazado: %s", esp_err_to_name(err));
        break;
    }
    case AGBLE_SERVER_ADVERTISING:
        /* El dispositivo se anuncia; muestra los UUID del servicio y características.
         * The device is advertising; displays service and characteristic UUIDs. */
        ESP_LOGI(TAG, "BLE agbot: servicio 0xFFF0, comandos 0xFFF1, estado 0xFFF2");
        break;
    case AGBLE_SERVER_CONNECTED:
        /* Cada nueva conexión comienza solicitando los motores detenidos.
         * Each new connection starts by requesting stopped motors. */
        motor_control_stop();
        ESP_LOGI(TAG, "Cliente BLE conectado");
        break;
    case AGBLE_SERVER_DISCONNECTED:
        /* Perder el enlace solicita una parada.
         * Losing the link requests a stop. */
        motor_control_stop();
        ESP_LOGI(TAG, "Cliente BLE desconectado; reanudando anuncios");
        break;
    case AGBLE_SERVER_ERROR:
        /* Ante un fallo BLE, solicita la parada y registra el código recibido.
         * On a BLE failure, requests a stop and logs the received code. */
        motor_control_stop();
        ESP_LOGE(TAG, "Error BLE: %d", event->status);
        break;
    }
}

void app_main(void)
{
    /* ESP-IDF ejecuta esta función al arrancar. ESP_ERROR_CHECK aborta si falla
     * una operación de inicio, en lugar de continuar con un inicio incompleto.
     * ESP-IDF runs this function at startup. ESP_ERROR_CHECK aborts if a startup
     * operation fails, instead of continuing with incomplete initialization. */

    /* Configura GPIO/PWM, deja los motores apagados e inicia su tarea;
     * on_motor_status recibirá sus actualizaciones periódicas.
     * Configures GPIO/PWM, leaves the motors off, and starts their task;
     * on_motor_status will receive its periodic updates. */
    ESP_ERROR_CHECK(motor_control_init(on_motor_status));
    /* Prepara la memoria NVS utilizada por la infraestructura Bluetooth.
     * Prepares the NVS storage used by the Bluetooth infrastructure. */
    ESP_ERROR_CHECK(nvs_flash_init());
    /* Inicia BLE con nombre agbot y registra el callback, sin contexto adicional.
     * Starts BLE as agbot and registers the callback without additional context. */
    ESP_ERROR_CHECK(agble_server_start("agbot", on_ble_event, NULL));
    /* Establece un estado inicial con todos los bytes en cero.
     * Sets an initial status with all bytes at zero. */
    const uint8_t initial_status[MOTOR_STATUS_LENGTH] = {0};
    ESP_ERROR_CHECK(agble_server_publish(initial_status, sizeof(initial_status)));
    /* Habilita las publicaciones periódicas desde la tarea de motores.
     * Enables periodic publishing from the motor task. */
    atomic_store(&ble_ready, true);
}
