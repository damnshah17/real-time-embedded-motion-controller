#include "drivers/motor.h"
#include "drivers/encoder.h"
#include "drivers/gpio.h"
#include "drivers/timer.h"
#include "drivers/watchdog.h"
#include "drivers/platform.h"
#include "platform/host/encoder_host.h"
#include "platform/host/gpio_host.h"
#include "platform/host/timer_host.h"
#include "platform/host/watchdog_host.h"
#include "platform/host/host_access.h"
#include "test_check.h"
#include <math.h>
#include <string.h>

static unsigned int depth;
static unsigned int entries;
static void enter(void) { ++depth; ++entries; }
static void leave(void) { --depth; }

static int check_motor(void)
{
    CHECK(motor_set_output(0.5f) == MOTOR_NOT_INITIALIZED);
    CHECK(motor_get_commanded_output() == 0.0f);
    motor_disable();
    motor_init();
    CHECK(motor_get_commanded_output() == 0.0f);
    CHECK(motor_set_output(0.4f) == MOTOR_OK && motor_get_commanded_output() == 0.4f);
    CHECK(motor_set_output(-0.75f) == MOTOR_OK && motor_get_commanded_output() == -0.75f);
    CHECK(motor_set_output(5.0f) == MOTOR_CLAMPED && motor_get_commanded_output() == 1.0f);
    CHECK(motor_set_output(-5.0f) == MOTOR_CLAMPED && motor_get_commanded_output() == -1.0f);
    CHECK(motor_set_output(NAN) == MOTOR_INVALID && motor_get_commanded_output() == 0.0f);
    CHECK(motor_set_output(INFINITY) == MOTOR_INVALID && motor_get_commanded_output() == 0.0f);
    CHECK(motor_set_output(-INFINITY) == MOTOR_INVALID && motor_get_commanded_output() == 0.0f);
    CHECK(motor_set_output(0.5f) == MOTOR_OK);
    motor_disable();
    CHECK(motor_get_commanded_output() == 0.0f);
    CHECK(motor_set_output(0.5f) == MOTOR_OK);
    motor_init();
    CHECK(motor_get_commanded_output() == 0.0f);
    return EXIT_SUCCESS;
}

static int check_encoder(void)
{
    encoder_host_state_t state = {123, 456};
    CHECK(!encoder_host_get_state(&state) && state.raw_count == 123 && state.reference_offset == 456);
    int32_t count = 99;
    CHECK(!encoder_get_count(&count) && count == 99);
    CHECK(!encoder_set_reference(0) && !encoder_host_set_count(1));
    encoder_init();
    CHECK(!encoder_get_count(NULL));
    CHECK(encoder_get_count(&count) && count == 0);
    CHECK(encoder_host_set_count(1000));
    CHECK(encoder_get_count(&count) && count == 1000);
    motor_init();
    CHECK(motor_set_output(0.4f) == MOTOR_OK);
    CHECK(encoder_get_count(&count) && count == 1000); /* No plant coupling. */
    CHECK(encoder_set_reference(0));
    CHECK(encoder_host_get_state(&state) && state.raw_count == 1000 && state.reference_offset == -1000);
    CHECK(!encoder_host_get_state(NULL));
    CHECK(encoder_get_count(&count) && count == 0);
    CHECK(encoder_host_set_count(1025));
    CHECK(encoder_get_count(&count) && count == 25);
    CHECK(encoder_set_reference(-100));
    CHECK(encoder_get_count(&count) && count == -100);
    CHECK(encoder_host_set_count(INT32_MAX));
    CHECK(encoder_set_reference(0));
    CHECK(encoder_host_set_count(INT32_MIN));
    CHECK(encoder_get_count(&count) && count == 1);
    CHECK(encoder_set_reference(INT32_MAX));
    CHECK(encoder_host_set_count(INT32_MIN + 1));
    CHECK(encoder_get_count(&count) && count == INT32_MIN);
    encoder_init();
    CHECK(encoder_get_count(&count) && count == 0);
    return EXIT_SUCCESS;
}

static int check_gpio(void)
{
    gpio_inputs_t inputs = {true, true, true};
    CHECK(!gpio_read_inputs(&inputs) && inputs.estop);
    CHECK(!gpio_host_set_estop(true));
    gpio_init();
    CHECK(!gpio_read_inputs(NULL));
    CHECK(gpio_read_inputs(&inputs) && !inputs.estop && !inputs.negative_limit && !inputs.positive_limit);
    CHECK(gpio_host_set_estop(true));
    CHECK(gpio_read_inputs(&inputs) && inputs.estop && !inputs.negative_limit && !inputs.positive_limit);
    CHECK(gpio_host_set_positive_limit(true));
    CHECK(gpio_host_set_negative_limit(true));
    CHECK(gpio_read_inputs(&inputs) && inputs.estop && inputs.negative_limit && inputs.positive_limit);
    CHECK(gpio_host_set_estop(false));
    CHECK(gpio_read_inputs(&inputs) && !inputs.estop && inputs.negative_limit && inputs.positive_limit);
    CHECK(gpio_host_set_negative_limit(false) && gpio_host_set_positive_limit(false));
    CHECK(gpio_read_inputs(&inputs) && !inputs.estop && !inputs.negative_limit && !inputs.positive_limit);
    return EXIT_SUCCESS;
}

static uint32_t source_time;
static uint32_t clock_source(void) { return source_time; }
static int check_timer(void)
{
    CHECK(timer_resolution_ms() == 0U);
    CHECK(!timer_host_advance_ms(1U));
    timer_init();
    CHECK(timer_now_ms() == 0U && timer_resolution_ms() == 1U);
    CHECK(timer_host_advance_ms(100U));
    const uint32_t start = timer_now_ms();
    CHECK(timer_host_advance_ms(7U));
    CHECK(timer_now_ms() >= start && timer_elapsed_ms(start, timer_now_ms()) == 7U);
    CHECK(!timer_host_set_source(clock_source, 10U)); /* Cannot swap an advanced epoch. */
    timer_init();
    CHECK(timer_host_advance_ms(UINT32_MAX - 4U));
    const uint32_t before_wrap = timer_now_ms();
    CHECK(timer_host_advance_ms(10U));
    CHECK(timer_now_ms() == 5U && timer_elapsed_ms(before_wrap, timer_now_ms()) == 10U);
    timer_init();
    CHECK(!timer_host_set_source(NULL, 10U));
    CHECK(!timer_host_set_source(clock_source, 0U));
    CHECK(timer_host_set_source(clock_source, 10U));
    CHECK(!timer_host_set_source(clock_source, 10U));
    CHECK(!timer_host_advance_ms(1U));
    CHECK(timer_resolution_ms() == 10U && timer_now_ms() == 0U);
    source_time = 20U;
    CHECK(timer_now_ms() == 20U);
    CHECK(timer_now_ms() == 20U); /* Reads do not invent elapsed time. */
    return EXIT_SUCCESS;
}

static int check_watchdog(void)
{
    watchdog_host_state_t state;
    CHECK(watchdog_host_get_state(&state) && !state.initialized);
    CHECK(!watchdog_refresh());
    CHECK(!watchdog_init(1000U)); /* Clock must exist first. */
    timer_init();
    CHECK(!watchdog_init(0U) && !watchdog_init(UINT32_MAX));
    CHECK(watchdog_init(1000U));
    CHECK(watchdog_host_get_state(&state) && state.initialized && state.refresh_count == 0U);
    CHECK(timer_host_advance_ms(75U) && watchdog_refresh());
    CHECK(watchdog_host_get_state(&state) && state.refresh_count == 1U && state.last_refresh_ms == 75U);
    CHECK(!watchdog_init(0U));
    CHECK(watchdog_host_get_state(&state) && state.refresh_count == 1U && state.timeout_ms == 1000U);
    CHECK(timer_host_advance_ms(2000U));
    CHECK(watchdog_host_get_state(&state) && state.refresh_count == 1U); /* No automatic refresh/fault. */
    CHECK(watchdog_refresh());
    CHECK(watchdog_host_get_state(&state) && state.refresh_count == 2U && state.last_refresh_ms == 2075U);
    CHECK(!watchdog_host_get_state(NULL));
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    CHECK(!host_access_configure(enter, NULL));
    CHECK(host_access_configure(enter, leave));
    int result = EXIT_FAILURE;
    if (strcmp(argv[1], "motor") == 0) { result = check_motor(); }
    else if (strcmp(argv[1], "encoder") == 0) { result = check_encoder(); }
    else if (strcmp(argv[1], "gpio") == 0) { result = check_gpio(); }
    else if (strcmp(argv[1], "timer") == 0) { result = check_timer(); }
    else if (strcmp(argv[1], "watchdog") == 0) { result = check_watchdog(); }
    CHECK(depth == 0U && entries != 0U);
    return result;
}
