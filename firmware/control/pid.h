#ifndef MOTION_PID_H
#define MOTION_PID_H
#include <stdbool.h>

typedef struct {
    float kp, ki, kd;
    float output_min, output_max;
    float integral_min, integral_max; /* Bounds on I contribution, in output units. */
} pid_config_t;
typedef struct {
    pid_config_t config;
    float integral, previous_measurement;
    bool initialized, sampled;
} pid_controller_t;

bool pid_init(pid_controller_t *pid, const pid_config_t *config);
void pid_reset(pid_controller_t *pid);
/* Derivative on measurement, zero on first sample. Explicit positive dt.
 * Invalid input/arithmetic leaves state and caller output unchanged. */
bool pid_update(pid_controller_t *pid, float target, float measurement, float dt, float *output);
#endif
