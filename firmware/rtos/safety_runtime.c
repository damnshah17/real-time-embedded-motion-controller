#include "rtos/runtime_internal.h"
#include "rtos/diagnostics.h"
#include "drivers/motor.h"
#include "drivers/platform.h"
#include "drivers/encoder.h"
#include "control/homing.h"

void runtime_safety_enforce(void)
{
    safety_manager_t *s = &runtime_data.safety;
    const app_state_t state = runtime_data.motion.state;
    const bool contact = s->inputs_valid && s->inputs.negative_limit;
    const bool home_stop = contact && (state == APP_HOMING || (s->home_contact && state != APP_MOVING));
    const bool unsafe = s->state != APP_IDLE || !s->healthy || home_stop;
    motor_safety_block_negative(s->home_contact && contact);
    s->requested_pwm = motor_get_requested_output();
    if (unsafe != motor_is_inhibited()) {
        if (unsafe) { s->requested_at_trip = s->requested_pwm; }
        motor_safety_inhibit(unsafe);
    }
    s->applied_pwm = motor_get_commanded_output();
    if (s->state != APP_IDLE) {
        if (s->zero_ms != s->detection_ms) { s->zero_ms = platform_time_ms(); }
        runtime_data.state = s->state;
        runtime_data.motor_output = 0.0f;
    }
}
void runtime_safety_evaluate(void)
{
    gpio_inputs_t inputs = {0};
    const bool valid = gpio_read_inputs(&inputs);
    uint32_t mask = 0U;
    const uint32_t now = platform_time_ms();
    for (unsigned int i = 0U; i < HEALTH_COUNT; ++i) {
        if (!task_health_fresh(&runtime_data.health[i], now, HEALTH_MAXIMUM_AGE_MS)) { mask |= 1U << i; }
    }
    motion_snapshot_t feedback = runtime_data.motion;
    feedback.feedback_valid = encoder_get_count(&feedback.position);
    safety_evaluate(&runtime_data.safety, now, inputs, valid, &feedback, motor_get_commanded_output(), mask);
    runtime_data.gpio = inputs; runtime_data.gpio_valid = valid;
    runtime_safety_enforce();
}
void runtime_safety_sync(motion_controller_t *m)
{
    const app_state_t state = runtime_data.safety.state;
    if (state == APP_IDLE) { return; }
    if (m->data.state == APP_MOVING || m->data.state == APP_STOPPING || m->data.state == APP_HOMING) {
        ++runtime_data.safety.aborted_moves;
    }
    if (m->data.state == APP_HOMING) {
        m->data.homing.elapsed_ms = platform_time_ms() - m->data.homing.started_ms;
        homing_abort(m, state == APP_ESTOP ? HOMING_ESTOPPED : HOMING_FAULTED);
    }
    m->data.state = state; m->data.target = m->data.position;
    m->data.pwm = m->data.pid_output = 0.0f; m->data.stable_cycles = 0U;
    pid_reset(&m->pid);
}
motion_result_t runtime_safety_command(motion_controller_t *m, motion_command_t command)
{
    if (command.type == CMD_ESTOP) {
        safety_estop(&runtime_data.safety, platform_time_ms());
        runtime_safety_enforce(); runtime_safety_sync(m);
        return MOTION_ACCEPTED;
    }
    if (command.type == CMD_RESET) {
        if (runtime_data.safety.state == APP_IDLE && m->data.state != APP_IDLE) { return MOTION_INVALID_STATE; }
        runtime_safety_evaluate();
        if (!safety_reset(&runtime_data.safety)) { return MOTION_INVALID_STATE; }
        motor_disable(); motor_safety_inhibit(false);
        m->data.state = APP_IDLE; m->data.target = m->data.position;
        m->data.pwm = m->data.pid_output = 0.0f; m->data.stable_cycles = 0U;
        pid_reset(&m->pid);
        return MOTION_ACCEPTED;
    }
    if (runtime_data.safety.state == APP_IDLE && m->data.state == APP_HOMING && command.type == CMD_STOP) {
        m->data.homing.elapsed_ms = platform_time_ms() - m->data.homing.started_ms;
        homing_abort(m, HOMING_STOPPED);
        ++runtime_data.safety.aborted_moves;
        m->data.state = APP_STOPPING;
        runtime_data.motion = m->data;
        return MOTION_ACCEPTED;
    }
    if (runtime_data.safety.state != APP_IDLE || motor_is_inhibited()) {
        const bool parked_home = runtime_data.safety.state == APP_IDLE && runtime_data.safety.healthy &&
            runtime_data.safety.home_contact && m->data.state == APP_IDLE;
        if (!parked_home) { return command.type == CMD_STOP ? MOTION_ACCEPTED : MOTION_INVALID_STATE; }
    }
    if (command.type == CMD_HOME) {
        const motion_result_t result = homing_begin(m, platform_time_ms());
        runtime_data.motion = m->data;
        if (result == MOTION_ACCEPTED) { runtime_safety_evaluate(); runtime_safety_sync(m); }
        return result;
    }
    if (m->data.state == APP_HOMING) {
        return MOTION_INVALID_STATE;
    }
    if (runtime_data.safety.home_contact && (command.type == CMD_MOVE_ABS || command.type == CMD_MOVE_REL)) {
        const int64_t target = command.type == CMD_MOVE_REL ? (int64_t)m->data.position + command.value : command.value;
        if (target <= m->data.position) { return MOTION_INVALID_STATE; }
        const motion_result_t result = motion_command(m, command);
        runtime_data.motion = m->data;
        runtime_safety_evaluate(); runtime_safety_sync(m);
        return result;
    }
    return motion_command(m, command);
}
bool runtime_control_update(motion_controller_t *m, float dt, uint32_t now)
{
    if (m->data.state != APP_HOMING) {
        if (runtime_data.safety.home_contact) {
            runtime_data.motion = m->data;
            runtime_safety_evaluate(); runtime_safety_sync(m);
        }
        return motion_update(m, dt);
    }
    gpio_inputs_t inputs = {0};
    if (!gpio_read_inputs(&inputs) || inputs.estop || inputs.positive_limit || inputs.negative_limit) {
        runtime_data.motion = m->data;
        runtime_safety_evaluate(); runtime_safety_sync(m);
        if (runtime_data.safety.state != APP_IDLE) { return true; }
    }
    if (!homing_update(m, inputs, now)) { return false; }
    if (m->data.homing.result == HOMING_TIMEOUT) {
        safety_homing_timeout(&runtime_data.safety, now);
        runtime_safety_enforce(); runtime_safety_sync(m);
    } else if (m->data.homing.result == HOMING_COMPLETE) {
        runtime_data.safety.home_contact = true;
        runtime_data.motion = m->data;
        runtime_safety_evaluate();
    }
    return true;
}
