#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <stdint.h>
#include "esp_err.h"
#include "motor_motion.h"

#define MOTOR_STATUS_LENGTH 5
#define MOTOR_STATUS_TIMEOUT 0x01u
#define MOTOR_STATUS_PAUSED  0x02u
#define MOTOR_STATUS_FAULT   0x04u

/* Estado cada 100 ms: botones, base %, PWM izquierdo %, PWM derecho %, flags.
 * Datos válidos durante el callback; no bloquear.
 * Status every 100 ms: buttons, base %, left PWM %, right PWM %, flags.
 * Data is valid during the callback; do not block. */
typedef void (*motor_status_callback_t)(const uint8_t status[MOTOR_STATUS_LENGTH]);

/* Inicializar una vez, antes de BLE. La tarea gestiona GPIO y PWM.
 * Initialize once, before BLE. The task manages GPIO and PWM. */
esp_err_t motor_control_init(motor_status_callback_t callback);

/* No bloquea; conserva el último comando. Enviar cada 100 ms.
 * Entrada inválida: solicita parada y devuelve ESP_ERR_INVALID_ARG.
 * Non-blocking; keeps the latest command. Send every 100 ms.
 * Invalid input: requests a stop and returns ESP_ERR_INVALID_ARG. */
esp_err_t motor_control_command(uint8_t buttons);
void motor_control_stop(void);

#endif
