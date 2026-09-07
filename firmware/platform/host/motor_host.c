#include "drivers/motor.h"
#include "platform/host/host_access.h"
#include <stdbool.h>
#include <math.h>

static bool initialized;
static float output;
static float requested;
static bool inhibited;
static bool negative_blocked;

void motor_init(void)
{
    output = 0.0f;
    requested = 0.0f;
    inhibited = false;
    negative_blocked = false;
    initialized = true;
}
motor_result_t motor_set_output(float duty_cycle)
{
    host_access_enter();
    motor_result_t result = MOTOR_OK;
    if (!initialized) { result = MOTOR_NOT_INITIALIZED; }
    else if (!isfinite(duty_cycle)) { requested = output = 0.0f; result = MOTOR_INVALID; }
    else {
        if (duty_cycle > 1.0f) { duty_cycle = 1.0f; result = MOTOR_CLAMPED; }
        else if (duty_cycle < -1.0f) { duty_cycle = -1.0f; result = MOTOR_CLAMPED; }
        requested = duty_cycle;
        const bool blocked = inhibited || (negative_blocked && duty_cycle < 0.0f);
        output = blocked ? 0.0f : duty_cycle;
        if (blocked) { result = MOTOR_INHIBITED; }
    }
    host_access_leave();
    return result;
}
void motor_disable(void)
{
    host_access_enter();
    output = 0.0f;
    requested = 0.0f;
    host_access_leave();
}
void motor_safety_inhibit(bool inhibit)
{
    host_access_enter();
    inhibited = inhibit;
    output = 0.0f;
    host_access_leave();
}
void motor_safety_block_negative(bool block)
{
    host_access_enter();
    negative_blocked = block;
    if (block && output < 0.0f) { output = 0.0f; }
    host_access_leave();
}
bool motor_is_inhibited(void)
{
    host_access_enter();
    const bool value = inhibited;
    host_access_leave();
    return value;
}
float motor_get_requested_output(void)
{
    host_access_enter();
    const float value = requested;
    host_access_leave();
    return value;
}
float motor_get_commanded_output(void)
{
    host_access_enter();
    const float result = output;
    host_access_leave();
    return result;
}
