#include "control/homing.h"
#include "safety/safety_manager.h"
#include "drivers/platform.h"
#include "drivers/encoder.h"
#include "drivers/motor.h"
#include "platform/host/encoder_host.h"
#include "test_check.h"
#include <string.h>
int main(int argc, char **argv)
{
    CHECK(argc == 2 && platform_init());
    motion_controller_t m;
    const motion_config_t c = motion_default_config();
    CHECK(motion_init(&m, &c));
    CHECK(encoder_host_set_count(1500) && motion_sample(&m, 0.01f));
    if (strcmp(argv[1], "reference") == 0) {
        CHECK(homing_begin(&m, 0U) == MOTION_ACCEPTED);
        CHECK(homing_update(&m, (gpio_inputs_t){0}, 10U) && motor_get_commanded_output() == HOMING_PWM);
        CHECK(encoder_host_set_count(123) && motion_sample(&m, 0.01f));
        m.pid.integral = 0.004f; m.data.stable_cycles = 5U;
        CHECK(homing_update(&m, (gpio_inputs_t){.negative_limit = true}, 100U));
        CHECK(m.data.state == APP_IDLE && m.data.homing.result == HOMING_COMPLETE && m.data.position == 0);
        CHECK(m.pid.integral == 0.0f && !m.pid.sampled && m.data.stable_cycles == 0U && m.data.target == 0);
        CHECK(motion_sample(&m, 0.01f) && m.data.velocity == 0.0f);
        CHECK(encoder_host_set_count(124) && motion_sample(&m, 0.01f));
        CHECK(m.data.position == 1 && m.data.velocity == 100.0f && motor_get_commanded_output() == 0.0f);
    } else if (strcmp(argv[1], "already") == 0) {
        CHECK(homing_begin(&m, 100U) == MOTION_ACCEPTED);
        CHECK(homing_update(&m, (gpio_inputs_t){.negative_limit = true}, 100U));
        CHECK(m.data.position == 0 && m.data.homing.elapsed_ms == 0U && motor_get_commanded_output() == 0.0f);
        CHECK(homing_begin(&m, 200U) == MOTION_ACCEPTED);
        CHECK(homing_update(&m, (gpio_inputs_t){.negative_limit = true}, 200U));
        CHECK(m.data.homing.completed == 2U && m.data.completed_moves == 0U);
    } else if (strcmp(argv[1], "timeout") == 0) {
        const uint32_t start = UINT32_MAX - 100U;
        CHECK(homing_begin(&m, start) == MOTION_ACCEPTED);
        CHECK(homing_update(&m, (gpio_inputs_t){0}, start + HOMING_TIMEOUT_MS - 1U));
        CHECK(m.data.homing.result == HOMING_RUNNING);
        CHECK(homing_update(&m, (gpio_inputs_t){.negative_limit = true}, start + HOMING_TIMEOUT_MS));
        CHECK(m.data.homing.result == HOMING_TIMEOUT && !m.data.homing.referenced && motor_get_commanded_output() == 0.0f);
        CHECK(m.data.position == 1500 && m.data.homing.completed == 0U);
    } else if (strcmp(argv[1], "abort") == 0) {
        CHECK(homing_begin(&m, 0U) == MOTION_ACCEPTED);
        CHECK(homing_update(&m, (gpio_inputs_t){0}, 10U));
        homing_abort(&m, HOMING_STOPPED);
        int32_t position;
        CHECK(encoder_get_count(&position) && position == 1500 && motor_get_commanded_output() == 0.0f);
        CHECK(m.data.homing.result == HOMING_STOPPED && m.data.homing.completed == 0U);
    } else if (strcmp(argv[1], "states") == 0) {
        const app_state_t states[] = {APP_MOVING, APP_STOPPING, APP_HOMING, APP_FAULT, APP_ESTOP};
        for (unsigned int i = 0U; i < sizeof(states) / sizeof(states[0]); ++i) {
            m.data.state = states[i]; CHECK(homing_begin(&m, 0U) == MOTION_INVALID_STATE);
        }
    } else if (strcmp(argv[1], "safety") == 0) {
        safety_manager_t s; safety_init(&s, 0U);
        m.data.state = APP_HOMING;
        safety_evaluate(&s, 200U, (gpio_inputs_t){.negative_limit = true}, true, &m.data, HOMING_PWM, 0U);
        CHECK(s.state == APP_IDLE && !s.candidate);
        safety_evaluate(&s, 220U, (gpio_inputs_t){.positive_limit = true}, true, &m.data, HOMING_PWM, 0U);
        CHECK(s.state == APP_FAULT && s.faults.active.code == FAULT_POSITIVE_LIMIT);
        safety_init(&s, 0U); m.data.state = APP_MOVING;
        safety_evaluate(&s, 200U, (gpio_inputs_t){.negative_limit = true}, true, &m.data, -0.8f, 0U);
        CHECK(s.state == APP_FAULT && s.faults.active.code == FAULT_NEGATIVE_LIMIT);
        safety_init(&s, 0U); m.data.state = APP_HOMING; m.data.velocity = 0.0f;
        safety_evaluate(&s, 0U, (gpio_inputs_t){0}, true, &m.data, HOMING_PWM, 0U);
        safety_evaluate(&s, 500U, (gpio_inputs_t){0}, true, &m.data, HOMING_PWM, 0U);
        CHECK(s.state == APP_FAULT && s.faults.active.code == FAULT_NO_MOTION_UNDER_COMMAND);
    } else if (strcmp(argv[1], "departure") == 0) {
        safety_manager_t s; safety_init(&s, 0U); s.home_contact = true;
        m.data.state = APP_IDLE; m.data.position = 0;
        safety_evaluate(&s, 200U, (gpio_inputs_t){.negative_limit = true}, true, &m.data, 0.0f, 0U);
        CHECK(s.state == APP_IDLE && s.home_contact);
        m.data.state = APP_MOVING; m.data.target = 500;
        safety_evaluate(&s, 220U, (gpio_inputs_t){.negative_limit = true}, true, &m.data, 0.8f, 0U);
        CHECK(s.state == APP_IDLE);
        safety_evaluate(&s, 240U, (gpio_inputs_t){0}, true, &m.data, 0.8f, 0U); CHECK(!s.home_contact);
        safety_evaluate(&s, 260U, (gpio_inputs_t){.negative_limit = true}, true, &m.data, 0.8f, 0U);
        CHECK(s.state == APP_FAULT);
        motor_safety_block_negative(true);
        CHECK(motor_set_output(-0.8f) == MOTOR_INHIBITED && motor_get_commanded_output() == 0.0f);
        CHECK(motor_set_output(0.3f) == MOTOR_OK);
        motor_safety_inhibit(true); CHECK(motor_set_output(0.3f) == MOTOR_INHIBITED);
    } else { CHECK(false); }
    return EXIT_SUCCESS;
}
