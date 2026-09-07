#include "safety/safety_manager.h"
#include "drivers/motor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failure line %d\n", __LINE__); return EXIT_FAILURE; } } while (0)
static void evaluate(safety_manager_t *s, motion_snapshot_t *m, uint32_t now, float pwm)
{ safety_evaluate(s, now, (gpio_inputs_t){0}, true, m, pwm, 0U); }
int main(int argc, char **argv)
{
    if (argc != 2) { return EXIT_FAILURE; }
    safety_manager_t s; safety_init(&s, 0U);
    motion_snapshot_t m = {.state = APP_MOVING, .feedback_valid = true, .position = 1000};
    if (strcmp(argv[1], "threshold") == 0) {
        evaluate(&s, &m, 0U, 0.299f); evaluate(&s, &m, 1000U, 0.299f);
        CHECK(!s.candidate && s.state == APP_IDLE);
        m.velocity = 25.01f; evaluate(&s, &m, 1001U, 0.3f); CHECK(!s.candidate);
        m.velocity = 25.0f; evaluate(&s, &m, 1020U, -0.3f); CHECK(s.candidate);
        evaluate(&s, &m, 1519U, -0.3f); CHECK(s.state == APP_IDLE && s.candidate_ms == 499U);
        evaluate(&s, &m, 1520U, -0.3f); CHECK(s.state == APP_FAULT && s.candidate_ms == 500U);
        CHECK(s.faults.active.code == FAULT_NO_MOTION_UNDER_COMMAND && !safety_reset(&s));
    } else if (strcmp(argv[1], "transient") == 0) {
        evaluate(&s, &m, 0U, 0.8f); evaluate(&s, &m, 499U, 0.8f);
        m.velocity = 100.0f; evaluate(&s, &m, 500U, 0.8f); CHECK(!s.candidate);
        m.velocity = 0.0f; evaluate(&s, &m, 520U, 0.8f); CHECK(s.candidate_ms == 0U);
        m.state = APP_STOPPING; evaluate(&s, &m, 2000U, 0.8f); CHECK(!s.candidate && s.state == APP_IDLE);
        m.state = APP_IDLE; evaluate(&s, &m, 3000U, 0.8f); CHECK(!s.candidate);
        m.state = APP_MOVING; evaluate(&s, &m, 4000U, 0.0f); CHECK(!s.candidate);
    } else if (strcmp(argv[1], "retention") == 0) {
        evaluate(&s, &m, 0U, 0.8f); evaluate(&s, &m, 500U, 0.8f);
        evaluate(&s, &m, 1000U, 0.0f); CHECK(s.faults.active.timestamp_ms == 500U && !safety_reset(&s));
        ++m.position; evaluate(&s, &m, 1020U, 0.0f); CHECK(!safety_reset(&s));
        ++m.position; evaluate(&s, &m, 1040U, 0.0f); CHECK(s.state == APP_FAULT && safety_reset(&s));
        CHECK(s.faults.active.code == FAULT_NONE && s.faults.history.code == FAULT_NO_MOTION_UNDER_COMMAND);
    } else if (strcmp(argv[1], "multiple") == 0) {
        safety_evaluate(&s, 200U, (gpio_inputs_t){.positive_limit = true}, true, &m, 0.0f, 0U);
        safety_evaluate(&s, 220U, (gpio_inputs_t){.estop = true}, true, &m, 0.0f, 1U);
        CHECK(s.state == APP_ESTOP && s.faults.active.code == FAULT_POSITIVE_LIMIT && !safety_reset(&s));
        safety_evaluate(&s, 240U, (gpio_inputs_t){0}, true, &m, 0.0f, 1U); CHECK(!safety_reset(&s));
        evaluate(&s, &m, 260U, 0.0f); CHECK(s.state == APP_ESTOP && safety_reset(&s));
    } else if (strcmp(argv[1], "wrap") == 0) {
        safety_init(&s, UINT32_MAX - 100U);
        evaluate(&s, &m, UINT32_MAX - 100U, 0.8f);
        evaluate(&s, &m, 398U, 0.8f); CHECK(s.state == APP_IDLE);
        evaluate(&s, &m, 399U, 0.8f); CHECK(s.state == APP_FAULT);
    } else if (strcmp(argv[1], "internal") == 0) {
        safety_internal_fault(&s, 20U); evaluate(&s, &m, 40U, 0.0f);
        CHECK(s.state == APP_FAULT && !safety_reset(&s) && s.faults.active.code == FAULT_INTERNAL);
    } else if (strcmp(argv[1], "gate") == 0) {
        motor_init(); CHECK(motor_set_output(0.8f) == MOTOR_OK);
        motor_safety_inhibit(true); CHECK(motor_get_commanded_output() == 0.0f);
        CHECK(motor_get_requested_output() == 0.8f);
        CHECK(motor_set_output(-0.8f) == MOTOR_INHIBITED && motor_get_commanded_output() == 0.0f);
        motor_disable(); CHECK(motor_is_inhibited());
        motor_safety_inhibit(false); CHECK(motor_get_commanded_output() == 0.0f);
        CHECK(motor_set_output(0.5f) == MOTOR_OK && motor_get_commanded_output() == 0.5f);
    } else { return EXIT_FAILURE; }
    puts("SAFETY_UNIT_OK"); return EXIT_SUCCESS;
}
