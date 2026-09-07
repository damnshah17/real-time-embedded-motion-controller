#ifndef MOTION_PLANT_MODEL_H
#define MOTION_PLANT_MODEL_H
#include <stdbool.h>
#include <stdint.h>

#define PLANT_DEFAULT_INITIAL_POSITION 1000.0

typedef struct {
    double motor_gain; /* axis units / s^2 at full duty */
    double damping; /* 1 / s */
    double max_velocity; /* axis units / s */
    double min_position;
    double max_position;
    double encoder_counts_per_unit;
    uint32_t step_ms;
} plant_config_t;

typedef struct {
    double position;
    double velocity;
    double acceleration; /* unconstrained drive/damping acceleration of last step */
    double applied_pwm; /* sampled duty of last step */
    int32_t raw_encoder_count;
    bool negative_limit;
    bool positive_limit;
    uint64_t steps;
} plant_state_t;

/* Single owner, one connected axis per process. Zero-initialize before first use.
 * Treat members as read-only; do not share this object between tasks/threads.
 * Only peripheral accesses use host access hooks, not the numerical computation. */
typedef struct {
    plant_config_t config;
    plant_state_t state;
    bool initialized;
    bool stalled, encoder_frozen; /* Host scenario owner only. */
} plant_model_t;

plant_config_t plant_default_config(void);
/* Requires initialized encoder/GPIO/motor peripherals. Invalid setup preserves
 * the prior model/peripherals. Successful init/reset disables drive, sets zero
 * velocity and publishes encoder/limits; encoder reference and E-stop survive. */
bool plant_init(plant_model_t *plant, const plant_config_t *config, double position);
bool plant_reset(plant_model_t *plant, double position);
/* Exactly one configured fixed step; samples motor_get_commanded_output().
 * Does not sleep, advance the platform timer, allocate or log. No ISR calls. */
bool plant_step(plant_model_t *plant);
bool plant_snapshot(const plant_model_t *plant, plant_state_t *state);

#endif
