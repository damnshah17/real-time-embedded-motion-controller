#include "control/homing.h"
#include "drivers/motor.h"
#include "drivers/encoder.h"

static void clear_control(motion_controller_t *m)
{
    motor_disable();
    m->data.pwm = m->data.pid_output = m->data.error = 0.0f;
    m->data.target = m->data.position; m->data.stable_cycles = 0U;
    pid_reset(&m->pid);
}
motion_result_t homing_begin(motion_controller_t *m, uint32_t now)
{
    if (!m->initialized || !m->data.feedback_valid) { return MOTION_INPUT_ERROR; }
    if (m->data.state != APP_IDLE) { return MOTION_INVALID_STATE; }
    clear_control(m);
    m->data.homing.result = HOMING_RUNNING;
    m->data.homing.started_ms = now; m->data.homing.elapsed_ms = 0U;
    m->data.homing.referenced = false;
    m->data.state = APP_HOMING;
    return MOTION_ACCEPTED;
}
void homing_abort(motion_controller_t *m, homing_result_t result)
{
    clear_control(m);
    if (m->data.homing.result == HOMING_RUNNING) { m->data.homing.result = result; }
}
bool homing_update(motion_controller_t *m, gpio_inputs_t inputs, uint32_t now)
{
    if (!m->initialized || !m->data.feedback_valid || m->data.state != APP_HOMING) { return false; }
    m->data.homing.elapsed_ms = now - m->data.homing.started_ms;
    if (inputs.estop || inputs.positive_limit) { clear_control(m); return false; }
    if (m->data.homing.elapsed_ms >= HOMING_TIMEOUT_MS) {
        homing_abort(m, HOMING_TIMEOUT); return true;
    }
    if (inputs.negative_limit) {
        clear_control(m);
        if (!encoder_set_reference(0)) { return false; }
        m->data.position = m->data.target = m->previous_count = 0;
        m->data.velocity = 0.0f;
        m->sampled = false; /* First sample after the reference change has zero velocity. */
        m->data.homing.result = HOMING_COMPLETE;
        m->data.homing.referenced = true;
        ++m->data.homing.completed;
        m->data.state = APP_IDLE;
        return true;
    }
    m->data.pwm = HOMING_PWM;
    return motor_set_output(HOMING_PWM) == MOTOR_OK;
}
const char *homing_result_name(homing_result_t result)
{
    switch (result) {
    case HOMING_NONE: return "NONE";
    case HOMING_RUNNING: return "RUNNING";
    case HOMING_COMPLETE: return "COMPLETE";
    case HOMING_STOPPED: return "STOPPED";
    case HOMING_ESTOPPED: return "ESTOPPED";
    case HOMING_TIMEOUT: return "TIMEOUT";
    default: return "FAULTED";
    }
}
