#include "plant_model.h"
#include "drivers/platform.h"
#include "drivers/motor.h"
#include "drivers/encoder.h"
#include "drivers/gpio.h"
#include "drivers/timer.h"
#include "platform/host/timer_host.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SNAPSHOT_PERIOD_MS = 500, DRIVE_STEPS = 1000, COAST_STEPS = 1000, REVERSE_STEPS = 2000 };
static bool quiet;

static bool observe(const plant_model_t *plant, const char *stage)
{
    plant_state_t state;
    int32_t count;
    gpio_inputs_t gpio;
    if (!plant_snapshot(plant, &state) || !encoder_get_count(&count) || !gpio_read_inputs(&gpio)) { return false; }
    if (!quiet) {
        printf("PLANT stage=%s logical_ms=%" PRIu32 " steps=%" PRIu64
               " pwm=%.2f position=%.6f velocity=%.6f raw=%" PRId32
               " encoder=%" PRId32 " limits=%u/%u\n",
               stage, timer_now_ms(), state.steps, state.applied_pwm, state.position, state.velocity,
               state.raw_encoder_count, count, gpio.negative_limit ? 1U : 0U, gpio.positive_limit ? 1U : 0U);
    }
    /* These scenarios retain reference zero; unit tests cover nonzero offsets. */
    return count == state.raw_encoder_count && gpio.negative_limit == state.negative_limit &&
        gpio.positive_limit == state.positive_limit;
}

static bool run_segment(plant_model_t *plant, float pwm, unsigned int steps, const char *stage)
{
    if (pwm == 0.0f) { motor_disable(); }
    else if (motor_set_output(pwm) != MOTOR_OK) { return false; }
    for (unsigned int i = 0U; i < steps; ++i) {
        if (!plant_step(plant) || !timer_host_advance_ms(plant->config.step_ms)) { return false; }
        if (plant->state.position < plant->config.min_position ||
            plant->state.position > plant->config.max_position ||
            fabs(plant->state.velocity) > plant->config.max_velocity) { return false; }
        if (i + 1U < steps && timer_now_ms() % SNAPSHOT_PERIOD_MS == 0U && !observe(plant, stage)) { return false; }
    }
    return observe(plant, stage);
}

static bool open_loop(plant_model_t *plant)
{
    const plant_state_t initial = plant->state;
    if (!run_segment(plant, 0.5f, DRIVE_STEPS, "positive")) { return false; }
    const plant_state_t driven = plant->state;
    if (driven.velocity <= 0.0 || driven.position <= initial.position ||
        driven.raw_encoder_count <= initial.raw_encoder_count) { return false; }
    if (!run_segment(plant, 0.0f, COAST_STEPS, "coast")) { return false; }
    const plant_state_t coast = plant->state;
    if (coast.applied_pwm != 0.0 || coast.velocity <= 0.0 || coast.velocity >= driven.velocity ||
        coast.position <= driven.position) { return false; }
    if (!run_segment(plant, -0.5f, REVERSE_STEPS, "reverse")) { return false; }
    return plant->state.velocity < 0.0 && plant->state.position < coast.position &&
        plant->state.raw_encoder_count < coast.raw_encoder_count;
}

static bool limits(plant_model_t *plant)
{
    const double start_distance = 10.0;
    if (!plant_reset(plant, plant->config.max_position - start_distance) ||
        !run_segment(plant, 1.0f, DRIVE_STEPS, "positive_limit")) { return false; }
    if (plant->state.position != plant->config.max_position || plant->state.velocity != 0.0 ||
        !plant->state.positive_limit || plant->state.negative_limit) { return false; }
    if (!run_segment(plant, -0.5f, 1U, "leave_positive_limit") || plant->state.positive_limit) { return false; }
    if (!plant_reset(plant, plant->config.min_position + start_distance) ||
        !run_segment(plant, -1.0f, DRIVE_STEPS, "negative_limit")) { return false; }
    return plant->state.position == plant->config.min_position && plant->state.velocity == 0.0 &&
        plant->state.negative_limit && !plant->state.positive_limit;
}

int main(int argc, char **argv)
{
    const char *scenario = "open-loop";
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--quiet") == 0) { quiet = true; }
        else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) { scenario = argv[++i]; }
        else { fputs("Usage: motion_controller_plant [--quiet] [--scenario open-loop|limits]\n", stderr); return EXIT_FAILURE; }
    }
    if (strcmp(scenario, "open-loop") != 0 && strcmp(scenario, "limits") != 0) {
        fputs("Unknown plant scenario\n", stderr); return EXIT_FAILURE;
    }
    plant_model_t plant = {0};
    const plant_config_t config = plant_default_config();
    if (!platform_init() || !plant_init(&plant, &config, PLANT_DEFAULT_INITIAL_POSITION)) { return EXIT_FAILURE; }
    if (!quiet) {
        printf("OPEN_LOOP_NO_PID scenario=%s dt_ms=%" PRIu32 " gain=%.1f damping=%.1f"
               " max_velocity=%.1f travel=%.1f..%.1f counts_per_unit=%.1f\n",
               scenario, config.step_ms, config.motor_gain, config.damping, config.max_velocity,
               config.min_position, config.max_position, config.encoder_counts_per_unit);
    }
    const bool passed = observe(&plant, "initial") &&
        (strcmp(scenario, "open-loop") == 0 ? open_loop(&plant) : limits(&plant));
    motor_disable();
    printf("%s scenario=%s logical_ms=%" PRIu32 " position=%.6f velocity=%.6f motor_command=%.2f (fixed-step simulation, no PID)\n",
           passed ? "PHASE4_OK" : "PHASE4_FAILED", scenario, timer_now_ms(),
           plant.state.position, plant.state.velocity, (double)motor_get_commanded_output());
    return passed && fflush(stdout) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
