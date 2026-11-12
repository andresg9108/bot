#include "motor_motion.h"

typedef struct {
    int direction;
    uint16_t left;
    uint16_t right;
} motor_targets_t;

static uint32_t advance(uint32_t value, uint32_t elapsed, uint32_t maximum)
{
    return elapsed >= maximum - value ? maximum : value + elapsed;
}

bool motor_buttons_valid(uint8_t buttons)
{
    /* Rechaza bits desconocidos y botones opuestos.
     * Rejects unknown bits and opposing buttons. */
    return !(buttons & ~0x0fu) &&
           (buttons & (MOTOR_FORWARD | MOTOR_BACKWARD)) != (MOTOR_FORWARD | MOTOR_BACKWARD) &&
           (buttons & (MOTOR_LEFT | MOTOR_RIGHT)) != (MOTOR_LEFT | MOTOR_RIGHT);
}

static uint16_t approach(uint16_t current, uint16_t target, uint32_t elapsed_ms)
{
    /* Suaviza los cambios de giro.
     * Smooths steering changes. */
    uint16_t step = elapsed_ms >= 500 ? MOTOR_PWM_MAX :
                    (MOTOR_PWM_MAX * elapsed_ms + 499) / 500;
    if (current < target) return target - current <= step ? target : current + step;
    return current - target <= step ? target : current - step;
}

static motor_targets_t accelerate_forward(motor_motion_t *motion, uint32_t elapsed_ms)
{
    /* Aumenta la potencia de ambos lados hacia adelante.
     * Increases power on both sides when moving forward. */
    motion->ramp_ms = advance(motion->ramp_ms, elapsed_ms, MOTOR_ACCELERATION_MS);
    uint16_t base = MOTOR_PWM_MAX * motion->ramp_ms / MOTOR_ACCELERATION_MS;
    return (motor_targets_t){.direction = 1, .left = base, .right = base};
}

static motor_targets_t move_backward(motor_motion_t *motion, uint32_t elapsed_ms)
{
    /* Usa la misma rampa, en sentido contrario.
     * Uses the same ramp in the opposite direction. */
    motor_targets_t targets = accelerate_forward(motion, elapsed_ms);
    targets.direction = -1;
    return targets;
}

static void turn_left(motor_targets_t *targets)
{
    /* Marcha atrás: izquierda describe la trayectoria del auto.
     * In reverse, left describes the car's trajectory. */
    if (targets->direction > 0) targets->left = targets->left * MOTOR_TURN_PERCENT / 100;
    else targets->right = targets->right * MOTOR_TURN_PERCENT / 100;
}

static void turn_right(motor_targets_t *targets)
{
    if (targets->direction > 0) targets->right = targets->right * MOTOR_TURN_PERCENT / 100;
    else targets->left = targets->left * MOTOR_TURN_PERCENT / 100;
}

void motor_motion_update(motor_motion_t *motion, bool received, uint8_t buttons,
                         uint32_t elapsed_ms)
{
    /* Cada comando renueva el plazo de comunicación.
     * Each command renews the communication timeout. */
    if (received) {
        motion->buttons = motor_buttons_valid(buttons) ? buttons : 0;
        motion->age_ms = 0;
        motion->timed_out = false;
    } else {
        motion->age_ms = advance(motion->age_ms, elapsed_ms, MOTOR_COMMAND_TIMEOUT_MS);
        if (motion->age_ms == MOTOR_COMMAND_TIMEOUT_MS && motion->buttons) {
            motion->buttons = 0;
            motion->timed_out = true;
        }
    }

    /* Soltar o invertir elimina inmediatamente la potencia anterior.
     * Releasing or reversing immediately removes the previous power. */
    int requested = (motion->buttons & MOTOR_FORWARD) ? 1 :
                    (motion->buttons & MOTOR_BACKWARD) ? -1 : 0;
    if (!requested || requested != motion->direction) {
        motion->direction = requested;
        motion->ramp_ms = 0;
        motion->left_pwm = 0;
        motion->right_pwm = 0;
        motion->guard_ms = requested ? MOTOR_REVERSE_PAUSE_MS : 0;
        return;
    }
    if (motion->guard_ms) {
        motion->guard_ms = elapsed_ms >= motion->guard_ms ? 0 : motion->guard_ms - elapsed_ms;
        /* Espera con ambas salidas PWM apagadas.
         * Waits with both PWM outputs off. */
        return;
    }

    /* Combina aceleración y giro a partir de los botones.
     * Combines acceleration and steering based on the buttons. */
    motor_targets_t targets = requested > 0 ? accelerate_forward(motion, elapsed_ms) :
                                            move_backward(motion, elapsed_ms);
    if (motion->buttons & MOTOR_LEFT) turn_left(&targets);
    if (motion->buttons & MOTOR_RIGHT) turn_right(&targets);
    motion->left_pwm = approach(motion->left_pwm, targets.left, elapsed_ms);
    motion->right_pwm = approach(motion->right_pwm, targets.right, elapsed_ms);
}
