#include "control/motion_controller.h"
#include "drivers/encoder.h"
#include "drivers/motor.h"
#include <math.h>
#include <stddef.h>

motion_config_t motion_default_config(void)
{
    return (motion_config_t){
        .pid = {.kp = 0.006f, .ki = 0.0002f, .kd = 0.0015f,
                .output_min = -0.8f, .output_max = 0.8f,
                .integral_min = -0.005f, .integral_max = 0.005f},
        .minimum = 0, .maximum = 5000, .tolerance = 3.0f,
        .completion_velocity = 25.0f, .dwell_cycles = 20U
    };
}
static bool fail(motion_controller_t *m)
{
    motor_disable();
    if (m != NULL && m->initialized) {
        m->data.state = APP_IDLE;
        m->data.feedback_valid = false;
        m->data.pwm = m->data.pid_output = 0.0f;
        m->data.stable_cycles = 0U;
        m->sampled = false;
        pid_reset(&m->pid);
    }
    return false;
}
bool motion_init(motion_controller_t *m, const motion_config_t *c)
{
    pid_controller_t pid;
    if (m == NULL || c == NULL || c->minimum >= c->maximum ||
        c->minimum < -1000000 || c->maximum > 1000000 ||
        !isfinite(c->tolerance) || c->tolerance < 0.0f ||
        !isfinite(c->completion_velocity) || c->completion_velocity < 0.0f || c->dwell_cycles == 0U ||
        c->pid.output_min < -1.0f || c->pid.output_min > 0.0f ||
        c->pid.output_max > 1.0f || c->pid.output_max < 0.0f || !pid_init(&pid, &c->pid)) { return false; }
    *m = (motion_controller_t){.config = *c, .pid = pid, .data = {.state = APP_IDLE}, .initialized = true};
    motor_disable();
    return true;
}
bool motion_sample(motion_controller_t *m, float dt)
{
    int32_t count;
    if (m == NULL || !m->initialized || !isfinite(dt) || dt <= 0.0f || dt > 1.0f ||
        !encoder_get_count(&count)) { return fail(m); }
    float velocity = 0.0f;
    if (m->sampled) {
        const uint32_t bits = (uint32_t)count - (uint32_t)m->previous_count;
        if (bits == UINT32_C(2147483648)) { return fail(m); } /* Ambiguous half-range jump. */
        const int32_t delta = bits <= INT32_MAX ? (int32_t)bits :
            INT32_MIN + (int32_t)(bits - UINT32_C(2147483648));
        velocity = (float)delta / dt;
        if (!isfinite(velocity)) { return fail(m); }
    }
    m->previous_count = count;
    m->sampled = true;
    m->data.position = count;
    m->data.velocity = velocity;
    m->data.error = (float)((int64_t)m->data.target - count);
    m->data.feedback_valid = true;
    return true;
}
motion_result_t motion_command(motion_controller_t *m, motion_command_t command)
{
    if (m == NULL || !m->initialized || !m->data.feedback_valid) { return MOTION_INPUT_ERROR; }
    if (command.type == CMD_STOP) {
        motor_disable();
        m->data.pwm = m->data.pid_output = 0.0f;
        pid_reset(&m->pid);
        if (m->data.state == APP_MOVING) { m->data.state = APP_STOPPING; m->data.stable_cycles = 0U; }
        return MOTION_ACCEPTED;
    }
    if (command.type != CMD_MOVE_ABS && command.type != CMD_MOVE_REL) { return MOTION_NOT_IMPLEMENTED; }
    if (m->data.state != APP_IDLE) { return MOTION_INVALID_STATE; }
    const int64_t target = command.type == CMD_MOVE_REL ? (int64_t)m->data.position + command.value : command.value;
    if (target < m->config.minimum || target > m->config.maximum) { return MOTION_OUT_OF_RANGE; }
    m->data.target = (int32_t)target;
    m->data.error = (float)(target - m->data.position);
    m->data.state = APP_MOVING;
    m->data.stable_cycles = 0U;
    m->data.pid_output = m->data.pwm = 0.0f;
    pid_reset(&m->pid);
    /* Seed measurement to avoid a first-update derivative kick on a new target. */
    m->pid.previous_measurement = (float)m->data.position;
    m->pid.sampled = true;
    return MOTION_ACCEPTED;
}
bool motion_update(motion_controller_t *m, float dt)
{
    if (m == NULL || !m->initialized || !m->data.feedback_valid || !isfinite(dt) || dt <= 0.0f || dt > 1.0f) { return fail(m); }
    motion_snapshot_t *s = &m->data;
    s->error = (float)((int64_t)s->target - s->position);
    if (s->state == APP_MOVING || s->state == APP_STOPPING) {
        const bool in_band = fabsf(s->velocity) <= m->config.completion_velocity &&
            (s->state == APP_STOPPING || fabsf(s->error) <= m->config.tolerance);
        s->stable_cycles = in_band ? s->stable_cycles + 1U : 0U;
        if (s->stable_cycles >= m->config.dwell_cycles) {
            if (s->state == APP_MOVING) { ++s->completed_moves; }
            s->state = APP_IDLE;
            pid_reset(&m->pid);
        }
    }
    if (s->state == APP_MOVING) {
        if (!pid_update(&m->pid, (float)s->target, (float)s->position, dt, &s->pid_output)) { return fail(m); }
        s->pwm = s->pid_output;
        if (motor_set_output(s->pwm) != MOTOR_OK) { return fail(m); }
    } else {
        s->pwm = s->pid_output = 0.0f;
        motor_disable();
    }
    return true;
}
const char *motion_result_name(motion_result_t result)
{
    switch (result) {
    case MOTION_ACCEPTED: return "ACK ACCEPTED";
    case MOTION_INVALID_STATE: return "ERR INVALID_STATE";
    case MOTION_OUT_OF_RANGE: return "ERR OUT_OF_RANGE";
    case MOTION_NOT_IMPLEMENTED: return "ERR NOT_IMPLEMENTED";
    default: return "ERR CONTROL_INPUT";
    }
}
