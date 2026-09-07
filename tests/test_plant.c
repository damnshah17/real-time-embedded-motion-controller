#include "plant_model.h"
#include "drivers/platform.h"
#include "drivers/motor.h"
#include "drivers/encoder.h"
#include "drivers/gpio.h"
#include "platform/host/encoder_host.h"
#include "platform/host/gpio_host.h"
#include "platform/host/host_access.h"
#include "test_check.h"
#include <float.h>
#include <math.h>
#include <string.h>

static plant_model_t plant;
static unsigned int depth;
static unsigned int entries;
static unsigned int outer_entries;
static void enter(void) { if (depth == 0U) { ++outer_entries; } ++depth; ++entries; }
static void leave(void) { --depth; }

static int advance(unsigned int count)
{
    for (unsigned int i = 0U; i < count; ++i) { CHECK(plant_step(&plant)); }
    return EXIT_SUCCESS;
}

static bool same_state(plant_state_t a, plant_state_t b)
{
    /* Compare fields, not struct padding. Exact equality is intentional for replay. */
    return a.position == b.position && a.velocity == b.velocity &&
        a.acceleration == b.acceleration && a.applied_pwm == b.applied_pwm &&
        a.raw_encoder_count == b.raw_encoder_count && a.negative_limit == b.negative_limit &&
        a.positive_limit == b.positive_limit && a.steps == b.steps;
}

static int check_zero(void)
{
    const plant_state_t initial = plant.state;
    CHECK(advance(1000U) == EXIT_SUCCESS);
    CHECK(plant.state.position == initial.position && plant.state.velocity == 0.0);
    CHECK(plant.state.acceleration == 0.0 && plant.state.applied_pwm == 0.0);
    CHECK(plant.state.raw_encoder_count == initial.raw_encoder_count && plant.state.steps == 1000U);
    return EXIT_SUCCESS;
}

static int check_direction(bool positive)
{
    int32_t count;
    const double start = plant.state.position;
    CHECK(motor_set_output(positive ? 0.5f : -0.5f) == MOTOR_OK);
    CHECK(advance(1000U) == EXIT_SUCCESS);
    CHECK(encoder_get_count(&count));
    if (positive) {
        CHECK(plant.state.velocity > 0.0 && plant.state.position > start && count > 1000);
    } else {
        CHECK(plant.state.velocity < 0.0 && plant.state.position < start && count < 1000);
    }
    CHECK(count == plant.state.raw_encoder_count);
    return EXIT_SUCCESS;
}

static int check_magnitude(void)
{
    CHECK(motor_set_output(0.25f) == MOTOR_OK && plant_step(&plant));
    const plant_state_t low = plant.state;
    CHECK(plant_reset(&plant, PLANT_DEFAULT_INITIAL_POSITION));
    CHECK(motor_set_output(0.75f) == MOTOR_OK && plant_step(&plant));
    CHECK(plant.state.acceleration > low.acceleration && plant.state.velocity > low.velocity);
    CHECK(plant.state.position > low.position);
    return EXIT_SUCCESS;
}

static int check_damping(void)
{
    for (unsigned int direction = 0U; direction < 2U; ++direction) {
        CHECK(plant_reset(&plant, PLANT_DEFAULT_INITIAL_POSITION));
        CHECK(motor_set_output(direction == 0U ? 0.5f : -0.5f) == MOTOR_OK);
        CHECK(advance(500U) == EXIT_SUCCESS);
        const double moving_position = plant.state.position;
        double speed = fabs(plant.state.velocity);
        motor_disable();
        for (unsigned int i = 0U; i < 1000U; ++i) {
            CHECK(plant_step(&plant));
            CHECK(plant.state.applied_pwm == 0.0 && fabs(plant.state.velocity) < speed);
            CHECK(plant.state.velocity != 0.0); /* Coast, not an instantaneous brake. */
            speed = fabs(plant.state.velocity);
        }
        CHECK(direction == 0U ? plant.state.position > moving_position : plant.state.position < moving_position);
        CHECK(speed < 5.0);
    }
    return EXIT_SUCCESS;
}

static int check_velocity(void)
{
    plant_config_t config = plant_default_config();
    config.max_velocity = 50.0;
    for (unsigned int direction = 0U; direction < 2U; ++direction) {
        CHECK(plant_init(&plant, &config, PLANT_DEFAULT_INITIAL_POSITION));
        CHECK(motor_set_output(direction == 0U ? 1.0f : -1.0f) == MOTOR_OK);
        for (unsigned int i = 0U; i < 2000U; ++i) {
            CHECK(plant_step(&plant));
            CHECK(fabs(plant.state.velocity) <= config.max_velocity);
        }
        CHECK(fabs(plant.state.velocity) == config.max_velocity);
    }
    return EXIT_SUCCESS;
}

static int check_limit(bool positive)
{
    const double bound = positive ? plant.config.max_position : plant.config.min_position;
    CHECK(plant_reset(&plant, bound + (positive ? -10.0 : 10.0)));
    CHECK(motor_set_output(positive ? 1.0f : -1.0f) == MOTOR_OK);
    for (unsigned int i = 0U; i < 2000U; ++i) {
        CHECK(plant_step(&plant));
        CHECK(plant.state.position >= plant.config.min_position && plant.state.position <= plant.config.max_position);
    }
    gpio_inputs_t inputs;
    CHECK(gpio_read_inputs(&inputs));
    CHECK(plant.state.position == bound && plant.state.velocity == 0.0);
    CHECK(inputs.positive_limit == positive && inputs.negative_limit != positive);
    /* Reversal releases the switch and moves inward immediately. */
    CHECK(motor_set_output(positive ? -0.5f : 0.5f) == MOTOR_OK && plant_step(&plant));
    CHECK(positive ? plant.state.position < bound : plant.state.position > bound);
    CHECK(gpio_read_inputs(&inputs) && !inputs.positive_limit && !inputs.negative_limit);
    return EXIT_SUCCESS;
}

static int check_encoder(void)
{
    plant_config_t config = plant_default_config();
    config.min_position = -100.0;
    config.max_position = 100.0;
    config.encoder_counts_per_unit = 4.0;
    const double positions[] = {0.0, 1.25, -1.25, 0.125, -0.125, 0.124, -0.124};
    const int32_t expected[] = {0, 5, -5, 1, -1, 0, 0};
    for (size_t i = 0U; i < sizeof(positions) / sizeof(positions[0]); ++i) {
        int32_t count;
        CHECK(plant_init(&plant, &config, positions[i]));
        CHECK(encoder_get_count(&count) && count == expected[i]);
        CHECK(plant.state.raw_encoder_count == expected[i]);
    }
    config.min_position = (double)INT32_MIN;
    config.max_position = (double)INT32_MAX;
    config.encoder_counts_per_unit = 1.0;
    CHECK(plant_init(&plant, &config, config.min_position) && plant.state.raw_encoder_count == INT32_MIN);
    CHECK(plant_reset(&plant, config.max_position) && plant.state.raw_encoder_count == INT32_MAX);
    return EXIT_SUCCESS;
}

static int check_reference(void)
{
    int32_t count;
    CHECK(encoder_set_reference(0));
    CHECK(motor_set_output(0.5f) == MOTOR_OK && advance(1000U) == EXIT_SUCCESS);
    CHECK(encoder_get_count(&count) && count == plant.state.raw_encoder_count - 1000);
    const double physical = plant.state.position;
    CHECK(encoder_set_reference(-25));
    CHECK(plant.state.position == physical && encoder_get_count(&count) && count == -25);
    const int32_t raw_before = plant.state.raw_encoder_count;
    CHECK(advance(200U) == EXIT_SUCCESS);
    CHECK(encoder_get_count(&count) && count == -25 + plant.state.raw_encoder_count - raw_before);
    return EXIT_SUCCESS;
}

static int sequence(void)
{
    static const struct { float pwm; unsigned int steps; } commands[] = {
        {0.5f, 1000U}, {0.0f, 1000U}, {-0.75f, 6000U}, {0.25f, 500U}
    };
    for (size_t i = 0U; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        CHECK(motor_set_output(commands[i].pwm) == MOTOR_OK);
        CHECK(advance(commands[i].steps) == EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

static int check_determinism(void)
{
    CHECK(sequence() == EXIT_SUCCESS);
    plant_state_t first;
    int32_t first_count, second_count;
    gpio_inputs_t first_gpio, second_gpio;
    CHECK(plant_snapshot(&plant, &first) && encoder_get_count(&first_count) && gpio_read_inputs(&first_gpio));
    CHECK(plant_reset(&plant, PLANT_DEFAULT_INITIAL_POSITION));
    CHECK(sequence() == EXIT_SUCCESS);
    CHECK(same_state(first, plant.state));
    CHECK(encoder_get_count(&second_count) && gpio_read_inputs(&second_gpio));
    CHECK(first_count == second_count && first_gpio.negative_limit == second_gpio.negative_limit &&
          first_gpio.positive_limit == second_gpio.positive_limit);
    return EXIT_SUCCESS;
}

static int check_reset(void)
{
    const plant_state_t initial = plant.state;
    CHECK(encoder_set_reference(17) && gpio_host_set_estop(true));
    CHECK(motor_set_output(0.5f) == MOTOR_OK && advance(1000U) == EXIT_SUCCESS);
    CHECK(plant_reset(&plant, PLANT_DEFAULT_INITIAL_POSITION));
    CHECK(same_state(initial, plant.state) && motor_get_commanded_output() == 0.0f);
    int32_t count;
    gpio_inputs_t inputs;
    CHECK(encoder_get_count(&count) && count == 17); /* Reference is not mechanical state. */
    CHECK(gpio_read_inputs(&inputs) && inputs.estop && !inputs.negative_limit && !inputs.positive_limit);
    return EXIT_SUCCESS;
}

static int check_invalid(void)
{
    plant_model_t empty = {0};
    plant_state_t output = {.position = 42.0};
    CHECK(!plant_step(&empty) && !plant_reset(&empty, 0.0));
    CHECK(!plant_snapshot(&empty, &output) && output.position == 42.0);
    CHECK(!plant_step(NULL) && !plant_snapshot(NULL, &output) && !plant_snapshot(&plant, NULL));
    const plant_state_t before = plant.state;
    plant_config_t config = plant_default_config();
    CHECK(!plant_init(NULL, &config, 0.0) && !plant_init(&plant, NULL, 0.0));
    CHECK(!plant_reset(&plant, NAN) && !plant_reset(&plant, -1.0) && !plant_reset(&plant, 5001.0));
    for (unsigned int i = 0U; i < 12U; ++i) {
        config = plant_default_config();
        switch (i) {
            case 0U: config.motor_gain = NAN; break;
            case 1U: config.damping = -1.0; break;
            case 2U: config.max_velocity = INFINITY; break;
            case 3U: config.min_position = config.max_position; break;
            case 4U: config.encoder_counts_per_unit = 0.0; break;
            case 5U: config.step_ms = 0U; break;
            case 6U: config.step_ms = 1001U; break;
            case 7U: config.damping = 1001.0; break;
            case 8U: config.encoder_counts_per_unit = 1e9; break;
            case 9U: config.motor_gain = DBL_MAX; config.max_velocity = DBL_MAX; break;
            case 10U: config.min_position = -INFINITY; break;
            default: config.motor_gain = 0.0; break;
        }
        CHECK(!plant_init(&plant, &config, PLANT_DEFAULT_INITIAL_POSITION));
        CHECK(same_state(before, plant.state));
    }
    int32_t count;
    CHECK(encoder_get_count(&count) && count == before.raw_encoder_count);
    CHECK(motor_set_output(NAN) == MOTOR_INVALID && plant_step(&plant));
    CHECK(plant.state.velocity == 0.0 && plant.state.applied_pwm == 0.0);
    return EXIT_SUCCESS;
}

static int check_ownership(void)
{
    CHECK(encoder_host_set_count(77) && gpio_host_set_positive_limit(true) && gpio_host_set_estop(true));
    int32_t count;
    CHECK(encoder_get_count(&count) && count == 77); /* Manual value holds until next publication. */
    const unsigned int transactions = outer_entries;
    CHECK(plant_step(&plant));
    CHECK(outer_entries - transactions == 2U); /* One PWM read and one atomic sensor publication. */
    gpio_inputs_t gpio;
    CHECK(encoder_get_count(&count) && count == 1000);
    CHECK(gpio_read_inputs(&gpio) && !gpio.negative_limit && !gpio.positive_limit && gpio.estop);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    CHECK(host_access_configure(enter, leave));
    const plant_config_t config = plant_default_config();
    CHECK(!plant_init(&plant, &config, PLANT_DEFAULT_INITIAL_POSITION)); /* Missing peripherals. */
    CHECK(platform_init() && plant_init(&plant, &config, PLANT_DEFAULT_INITIAL_POSITION));
    int result = EXIT_FAILURE;
    if (strcmp(argv[1], "zero") == 0) { result = check_zero(); }
    else if (strcmp(argv[1], "positive") == 0) { result = check_direction(true); }
    else if (strcmp(argv[1], "negative") == 0) { result = check_direction(false); }
    else if (strcmp(argv[1], "magnitude") == 0) { result = check_magnitude(); }
    else if (strcmp(argv[1], "damping") == 0) { result = check_damping(); }
    else if (strcmp(argv[1], "velocity") == 0) { result = check_velocity(); }
    else if (strcmp(argv[1], "positive_limit") == 0) { result = check_limit(true); }
    else if (strcmp(argv[1], "negative_limit") == 0) { result = check_limit(false); }
    else if (strcmp(argv[1], "encoder") == 0) { result = check_encoder(); }
    else if (strcmp(argv[1], "reference") == 0) { result = check_reference(); }
    else if (strcmp(argv[1], "determinism") == 0) { result = check_determinism(); }
    else if (strcmp(argv[1], "reset") == 0) { result = check_reset(); }
    else if (strcmp(argv[1], "invalid") == 0) { result = check_invalid(); }
    else if (strcmp(argv[1], "ownership") == 0) { result = check_ownership(); }
    CHECK(depth == 0U && entries > 0U);
    return result;
}
