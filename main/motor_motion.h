#ifndef MOTOR_MOTION_H
#define MOTOR_MOTION_H

#include <stdbool.h>
#include <stdint.h>

#define MOTOR_FORWARD  0x01u
#define MOTOR_BACKWARD 0x02u
#define MOTOR_LEFT     0x04u
#define MOTOR_RIGHT    0x08u

#define MOTOR_ACCELERATION_MS 3000u
#define MOTOR_REVERSE_PAUSE_MS 100u
#define MOTOR_COMMAND_TIMEOUT_MS 500u
#define MOTOR_TURN_PERCENT 50u
#define MOTOR_PWM_MAX 1023u

/* Estado interno; todos los campos en cero significan detenido.
 * Internal state; all fields at zero mean stopped. */
typedef struct {
    uint8_t buttons;
    int direction; /* -1: atrás, 0: detenido, 1: adelante.
                    * -1: backward, 0: stopped, 1: forward. */
    uint32_t ramp_ms;
    uint32_t guard_ms;
    uint32_t age_ms;
    uint16_t left_pwm;
    uint16_t right_pwm;
    bool timed_out;
} motor_motion_t;

bool motor_buttons_valid(uint8_t buttons);
void motor_motion_update(motor_motion_t *motion, bool received, uint8_t buttons,
                         uint32_t elapsed_ms);

#endif
