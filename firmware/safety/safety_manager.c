#include "safety/safety_manager.h"
#include <math.h>
#include <stdlib.h>
void safety_init(safety_manager_t *s, uint32_t now)
{ *s = (safety_manager_t){.state = APP_IDLE, .startup_ms = now}; }
static void trip(safety_manager_t *s, fault_code_t code, uint32_t now, uint32_t source)
{
    s->home_contact = false;
    fault_raise(&s->faults, code, now, source);
    if (s->state != APP_ESTOP && s->state != APP_FAULT) { s->state = APP_FAULT; s->detection_ms = now; }
}
void safety_estop(safety_manager_t *s, uint32_t now)
{
    s->home_contact = false;
    if (s->state != APP_ESTOP) { s->detection_ms = now; }
    s->state = APP_ESTOP;
}
void safety_internal_fault(safety_manager_t *s, uint32_t now)
{ s->internal_active = true; trip(s, FAULT_INTERNAL, now, 0U); }
void safety_homing_timeout(safety_manager_t *s, uint32_t now)
{ trip(s, FAULT_HOMING_TIMEOUT, now, 0U); }
void safety_evaluate(safety_manager_t *s, uint32_t now, gpio_inputs_t inputs, bool valid,
                     const motion_snapshot_t *m, float pwm, uint32_t unhealthy_mask)
{
    s->inputs = inputs; s->inputs_valid = valid;
    s->unhealthy_mask = unhealthy_mask; s->healthy = unhealthy_mask == 0U;
    if (!valid) { safety_internal_fault(s, now); }
    if (inputs.estop) { safety_estop(s, now); }
    if (inputs.positive_limit) { trip(s, FAULT_POSITIVE_LIMIT, now, 0U); }
    if (valid && !inputs.negative_limit) { s->home_contact = false; }
    const bool expected_home = m->state == APP_HOMING && s->state == APP_IDLE;
    const bool departing = m->state == APP_MOVING && m->target > m->position && pwm >= 0.0f;
    const bool parked = (m->state == APP_IDLE || m->state == APP_STOPPING) && pwm == 0.0f;
    if (inputs.negative_limit && !expected_home && !(s->home_contact && (parked || departing))) {
        trip(s, FAULT_NEGATIVE_LIMIT, now, 0U);
    }
    if (!s->healthy && now - s->startup_ms >= SAFETY_STARTUP_GRACE_MS) {
        trip(s, FAULT_WATCHDOG, now, unhealthy_mask);
    }
    if (s->encoder_recovery_required && m->feedback_valid &&
        llabs((long long)m->position - s->fault_position) >= 2LL) { s->encoder_recovery_required = false; }
    const bool qualifying = s->state == APP_IDLE && (m->state == APP_MOVING || m->state == APP_HOMING) &&
        !(expected_home && inputs.negative_limit) &&
        m->feedback_valid && fabsf(pwm) >= SAFETY_STALL_PWM && fabsf(m->velocity) <= SAFETY_STALL_VELOCITY;
    if (qualifying) {
        if (!s->candidate) { s->candidate = true; s->candidate_start_ms = now; }
        s->candidate_ms = now - s->candidate_start_ms;
        if (s->candidate_ms >= SAFETY_STALL_TIMEOUT_MS) {
            s->encoder_recovery_required = true; s->fault_position = m->position;
            trip(s, FAULT_NO_MOTION_UNDER_COMMAND, now, 0U);
        }
    } else { s->candidate = false; s->candidate_ms = 0U; }
    /* Recovery checks every current hazard, including hazards masked by first-fault retention. */
    s->faults.active.condition_active = s->faults.active.code != FAULT_NONE &&
        (!valid || inputs.positive_limit || inputs.negative_limit || !s->healthy ||
         s->encoder_recovery_required || s->internal_active);
}
bool safety_reset(safety_manager_t *s)
{
    if (!s->inputs_valid || s->inputs.estop || s->inputs.positive_limit || s->inputs.negative_limit ||
        !s->healthy || s->encoder_recovery_required || s->internal_active || !fault_reset(&s->faults)) { return false; }
    s->state = APP_IDLE; s->candidate = false; s->candidate_ms = 0U;
    return true;
}
