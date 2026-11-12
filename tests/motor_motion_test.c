#include <assert.h>
#include <stdio.h>
#include "motor_motion.h"

static void tick(motor_motion_t *motion, uint8_t buttons, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        motor_motion_update(motion, true, buttons, 20);
        assert(motion->left_pwm <= MOTOR_PWM_MAX);
        assert(motion->right_pwm <= MOTOR_PWM_MAX);
    }
}

static void test_commands(void)
{
    for (unsigned buttons = 0; buttons < 256; ++buttons) {
        bool expected = buttons == 0 || buttons == MOTOR_FORWARD || buttons == MOTOR_BACKWARD ||
                        buttons == MOTOR_LEFT || buttons == MOTOR_RIGHT ||
                        buttons == (MOTOR_FORWARD | MOTOR_LEFT) ||
                        buttons == (MOTOR_FORWARD | MOTOR_RIGHT) ||
                        buttons == (MOTOR_BACKWARD | MOTOR_LEFT) ||
                        buttons == (MOTOR_BACKWARD | MOTOR_RIGHT);
        assert(motor_buttons_valid(buttons) == expected);
    }
}

static void test_acceleration_and_release(void)
{
    motor_motion_t motion = {0};
    tick(&motion, MOTOR_FORWARD, 6);
    assert(motion.ramp_ms == 0);
    assert(motion.left_pwm == 0 && motion.right_pwm == 0);
    tick(&motion, MOTOR_FORWARD, 75);
    assert(motion.ramp_ms == 1500);
    assert(motion.left_pwm == MOTOR_PWM_MAX / 2);
    assert(motion.left_pwm == motion.right_pwm);
    tick(&motion, MOTOR_FORWARD, 75);
    assert(motion.left_pwm == MOTOR_PWM_MAX && motion.right_pwm == MOTOR_PWM_MAX);
    tick(&motion, MOTOR_FORWARD, 100);
    assert(motion.ramp_ms == MOTOR_ACCELERATION_MS);
    tick(&motion, 0, 1);
    assert(motion.direction == 0 && motion.ramp_ms == 0);
    assert(motion.left_pwm == 0 && motion.right_pwm == 0);
    tick(&motion, MOTOR_LEFT, 10);
    assert(motion.direction == 0 && motion.left_pwm == 0 && motion.right_pwm == 0);
}

static void test_steering(void)
{
    motor_motion_t motion = {0};
    tick(&motion, MOTOR_FORWARD | MOTOR_LEFT, 81);
    assert(motion.ramp_ms == 1500);
    assert(motion.left_pwm == (MOTOR_PWM_MAX / 2) / 2);
    assert(motion.right_pwm == MOTOR_PWM_MAX / 2);
    tick(&motion, MOTOR_FORWARD | MOTOR_LEFT, 100);
    assert(motion.left_pwm == MOTOR_PWM_MAX / 2 && motion.right_pwm == MOTOR_PWM_MAX);
    uint16_t previous = motion.left_pwm;
    tick(&motion, MOTOR_FORWARD, 1);
    assert(motion.left_pwm > previous && motion.left_pwm < MOTOR_PWM_MAX);
    tick(&motion, MOTOR_FORWARD, 30);
    assert(motion.left_pwm == MOTOR_PWM_MAX);
    tick(&motion, MOTOR_FORWARD | MOTOR_RIGHT, 30);
    assert(motion.left_pwm == MOTOR_PWM_MAX && motion.right_pwm == MOTOR_PWM_MAX / 2);

    motion = (motor_motion_t){0};
    tick(&motion, MOTOR_BACKWARD | MOTOR_LEFT, 181);
    assert(motion.direction == -1);
    assert(motion.left_pwm == MOTOR_PWM_MAX && motion.right_pwm == MOTOR_PWM_MAX / 2);
    tick(&motion, MOTOR_BACKWARD | MOTOR_RIGHT, 30);
    assert(motion.left_pwm == MOTOR_PWM_MAX / 2 && motion.right_pwm == MOTOR_PWM_MAX);
}

static void test_reverse_pause(void)
{
    motor_motion_t motion = {0};
    tick(&motion, MOTOR_FORWARD, 181);
    tick(&motion, MOTOR_BACKWARD, 1);
    assert(motion.direction == -1 && motion.guard_ms == MOTOR_REVERSE_PAUSE_MS);
    assert(motion.left_pwm == 0 && motion.right_pwm == 0 && motion.ramp_ms == 0);
    tick(&motion, MOTOR_BACKWARD, 5);
    assert(motion.guard_ms == 0 && motion.left_pwm == 0 && motion.right_pwm == 0);
    tick(&motion, MOTOR_BACKWARD, 1);
    assert(motion.left_pwm > 0 && motion.right_pwm > 0 && motion.ramp_ms == 20);
}

static void test_timeout_and_invalid_input(void)
{
    motor_motion_t motion = {0};
    tick(&motion, MOTOR_FORWARD, 100);
    for (unsigned i = 0; i < 24; ++i) motor_motion_update(&motion, false, 0, 20);
    assert(!motion.timed_out && motion.left_pwm > 0);
    motor_motion_update(&motion, false, 0, 20);
    assert(motion.timed_out && motion.buttons == 0 && motion.direction == 0);
    assert(motion.left_pwm == 0 && motion.right_pwm == 0);
    tick(&motion, MOTOR_FORWARD, 1);
    assert(!motion.timed_out && motion.guard_ms == MOTOR_REVERSE_PAUSE_MS);
    tick(&motion, MOTOR_FORWARD, 100);
    tick(&motion, MOTOR_FORWARD | MOTOR_BACKWARD, 1);
    assert(motion.buttons == 0 && motion.left_pwm == 0 && motion.right_pwm == 0);
    tick(&motion, MOTOR_FORWARD, 100);
    motor_motion_update(&motion, false, 0, UINT32_MAX);
    assert(motion.timed_out && motion.left_pwm == 0 && motion.right_pwm == 0);
}

int main(void)
{
    test_commands();
    test_acceleration_and_release();
    test_steering();
    test_reverse_pause();
    test_timeout_and_invalid_input();
    puts("motor_motion: todas las pruebas pasaron");
    return 0;
}
