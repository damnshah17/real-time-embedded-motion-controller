#include "control/pid.h"
#include <math.h>
#include <stddef.h>

static float clamp(float value, float low, float high)
{ return value < low ? low : (value > high ? high : value); }

bool pid_init(pid_controller_t *pid, const pid_config_t *c)
{
    if (pid == NULL || c == NULL || !isfinite(c->kp) || c->kp < 0.0f ||
        !isfinite(c->ki) || c->ki < 0.0f || !isfinite(c->kd) || c->kd < 0.0f ||
        !isfinite(c->output_min) || !isfinite(c->output_max) || c->output_min >= c->output_max ||
        !isfinite(c->integral_min) || !isfinite(c->integral_max) ||
        c->integral_min > 0.0f || c->integral_max < 0.0f || c->integral_min > c->integral_max) { return false; }
    *pid = (pid_controller_t){.config = *c, .initialized = true};
    return true;
}
void pid_reset(pid_controller_t *pid)
{
    if (pid != NULL) { pid->integral = 0.0f; pid->previous_measurement = 0.0f; pid->sampled = false; }
}
bool pid_update(pid_controller_t *pid, float target, float measurement, float dt, float *output)
{
    if (pid == NULL || !pid->initialized || output == NULL || !isfinite(target) ||
        !isfinite(measurement) || !isfinite(dt) || dt <= 0.0f) { return false; }
    const pid_config_t *c = &pid->config;
    const float error = target - measurement;
    const float candidate = pid->integral + c->ki * error * dt;
    const float derivative = pid->sampled ? -(measurement - pid->previous_measurement) / dt : 0.0f;
    if (!isfinite(error) || !isfinite(candidate) || !isfinite(derivative)) { return false; }
    /* One anti-windup strategy: clamp the integral contribution. */
    const float integral = clamp(candidate, c->integral_min, c->integral_max);
    const float sum = c->kp * error + integral + c->kd * derivative;
    if (!isfinite(sum)) { return false; }
    *output = clamp(sum, c->output_min, c->output_max);
    pid->integral = integral;
    pid->previous_measurement = measurement;
    pid->sampled = true;
    return true;
}
