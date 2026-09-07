#include "plant_model.h"
#include "drivers/motor.h"
#include "drivers/encoder.h"
#include "drivers/gpio.h"
#include "platform/host/encoder_host.h"
#include "platform/host/gpio_host.h"
#include "platform/host/host_access.h"
#include <math.h>
#include <stddef.h>

plant_config_t plant_default_config(void)
{
    return (plant_config_t){
        .motor_gain = 2000.0, .damping = 4.0, .max_velocity = 600.0,
        .min_position = 0.0, .max_position = 5000.0,
        .encoder_counts_per_unit = 1.0, .step_ms = 1U
    };
}

static bool valid_config(const plant_config_t *c)
{
    if (c == NULL || !isfinite(c->motor_gain) || c->motor_gain <= 0.0 ||
        !isfinite(c->damping) || c->damping < 0.0 ||
        !isfinite(c->max_velocity) || c->max_velocity <= 0.0 ||
        !isfinite(c->min_position) || !isfinite(c->max_position) ||
        c->min_position >= c->max_position ||
        !isfinite(c->encoder_counts_per_unit) || c->encoder_counts_per_unit <= 0.0 ||
        c->step_ms == 0U || c->step_ms > 1000U) { return false; }
    const double dt = (double)c->step_ms / 1000.0;
    const double acceleration_bound = c->motor_gain + c->damping * c->max_velocity;
    const double low_count = c->min_position * c->encoder_counts_per_unit;
    const double high_count = c->max_position * c->encoder_counts_per_unit;
    /* Non-oscillatory free decay, representable intermediate arithmetic and raw
     * counts. Reject impossible configurations instead of saturating encoders. */
    return c->damping * dt <= 1.0 && isfinite(acceleration_bound) &&
        isfinite(c->max_velocity + acceleration_bound * dt) &&
        isfinite(c->min_position - c->max_velocity * dt) &&
        isfinite(c->max_position + c->max_velocity * dt) &&
        isfinite(low_count) && isfinite(high_count) &&
        low_count >= (double)INT32_MIN && high_count <= (double)INT32_MAX;
}

static void derive_peripherals(const plant_config_t *c, plant_state_t *state)
{
    /* C round: nearest integer, halfway values away from zero. Range was checked. */
    state->raw_encoder_count = (int32_t)round(state->position * c->encoder_counts_per_unit);
    state->negative_limit = state->position <= c->min_position;
    state->positive_limit = state->position >= c->max_position;
}

static bool publish(const plant_state_t *state, bool frozen)
{
    int32_t count;
    gpio_inputs_t gpio;
    /* Hooks support nesting. Publish the raw count and both limits as one short
     * peripheral transaction. No computation or output under this outer hook. */
    host_access_enter();
    const bool ready = encoder_get_count(&count) && gpio_read_inputs(&gpio);
    bool result = false;
    if (ready) {
        result = (frozen || encoder_host_set_count(state->raw_encoder_count)) &&
            gpio_host_set_negative_limit(state->negative_limit) &&
            gpio_host_set_positive_limit(state->positive_limit);
    }
    host_access_leave();
    return result;
}

bool plant_init(plant_model_t *plant, const plant_config_t *config, double position)
{
    if (plant == NULL || !valid_config(config) || !isfinite(position) ||
        position < config->min_position || position > config->max_position) { return false; }
    plant_state_t state = {.position = position};
    derive_peripherals(config, &state);
    if (!publish(&state, false)) { return false; }
    motor_disable();
    *plant = (plant_model_t){.config = *config, .state = state, .initialized = true};
    return true;
}

bool plant_reset(plant_model_t *plant, double position)
{
    if (plant == NULL || !plant->initialized) { return false; }
    return plant_init(plant, &plant->config, position);
}

bool plant_step(plant_model_t *plant)
{
    if (plant == NULL || !plant->initialized || plant->state.steps == UINT64_MAX) { return false; }
    plant_state_t next = plant->state;
    const plant_config_t *c = &plant->config;
    const double dt = (double)c->step_ms / 1000.0;
    next.applied_pwm = (double)motor_get_commanded_output();
    next.acceleration = c->motor_gain * next.applied_pwm - c->damping * next.velocity;
    next.velocity += next.acceleration * dt;
    if (next.velocity > c->max_velocity) { next.velocity = c->max_velocity; }
    if (next.velocity < -c->max_velocity) { next.velocity = -c->max_velocity; }
    /* Semi-implicit Euler: integrate position using the updated velocity. */
    next.position += next.velocity * dt;
    if (plant->stalled) { next.position = plant->state.position; next.velocity = 0.0; next.acceleration = 0.0; }
    if (next.position <= c->min_position) {
        next.position = c->min_position;
        if (next.velocity < 0.0) { next.velocity = 0.0; }
    }
    if (next.position >= c->max_position) {
        next.position = c->max_position;
        if (next.velocity > 0.0) { next.velocity = 0.0; }
    }
    ++next.steps;
    derive_peripherals(c, &next);
    if (!publish(&next, plant->encoder_frozen)) { return false; }
    plant->state = next;
    return true;
}

bool plant_snapshot(const plant_model_t *plant, plant_state_t *state)
{
    if (plant == NULL || state == NULL || !plant->initialized) { return false; }
    *state = plant->state;
    return true;
}
