#ifndef MOTION_CONTROL_METRICS_H
#define MOTION_CONTROL_METRICS_H
#include "control/motion_controller.h"
typedef struct {
    int32_t start, target, final_position;
    float final_error, overshoot, maximum_pwm;
    uint32_t elapsed_ms, completion_ms, settling_ms;
    uint64_t output_hash;
    bool in_band, completed;
} control_metrics_t;
void control_metrics_init(control_metrics_t *metrics, int32_t start, int32_t target);
void control_metrics_sample(control_metrics_t *metrics, const motion_snapshot_t *sample,
                            const motion_config_t *config, uint32_t elapsed_ms);
#endif
