#include "FreeRTOS.h"
#include "task.h"
#include "rtos/runtime.h"
#include "rtos/diagnostics.h"
#include "rtos/hooks.h"
#include "drivers/platform.h"
#include "platform/host/platform_host.h"
#include "platform/host/input_host.h"
#include "platform/host/telemetry_host.h"
#include "platform/host/timer_host.h"
#include "platform/host/host_access.h"
#include "platform/host/encoder_host.h"
#include "platform/host/gpio_host.h"
#include "platform/host/watchdog_host.h"
#include "platform/host/uart_host.h"
#include "drivers/motor.h"
#include "drivers/timer.h"
#include "control/homing.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { NOMINAL, BURST, RX_OVERFLOW, DIAGNOSTIC_LOSS, PERIPHERALS, UART_LONG, UART_TX_FAILURE } scenario_t;
static scenario_t scenario;
static host_input_event_t events[HOST_SCRIPT_CAPACITY];
static size_t event_count;
static StaticTask_t harness_control;
static StackType_t harness_stack[configMINIMAL_STACK_SIZE];
static runtime_snapshot_t final_snapshot;
static host_input_stats_t final_input;
static volatile bool completed;
static volatile bool parked;
static volatile bool injection_ok = true;
static uart_host_stats_t final_uart;
static watchdog_host_state_t final_watchdog;
static void lock_peripherals(void) { taskENTER_CRITICAL(); }
static void unlock_peripherals(void) { taskEXIT_CRITICAL(); }
static bool failing_tx_sink(const uint8_t *data, size_t length)
{
    (void)data;
    (void)length;
    return false;
}

static uint32_t rtos_clock_ms(void)
{
    return (uint32_t)xTaskGetTickCount() * (uint32_t)portTICK_PERIOD_MS;
}

static void add_event(uint32_t at_ms, const char *text)
{
    configASSERT(event_count < HOST_SCRIPT_CAPACITY);
    host_input_event_t *event = &events[event_count++];
    event->at_ms = at_ms;
    event->line.length = strlen(text);
    configASSERT(event->line.length < COMMAND_LINE_CAPACITY);
    memcpy(event->line.text, text, event->line.length);
}

static void prepare_script(void)
{
    if (scenario == BURST || scenario == RX_OVERFLOW) {
        for (unsigned int i = 0U; i < HOST_SCRIPT_CAPACITY; ++i) {
            add_event(scenario == BURST && i >= 16U ? 140U : 100U, "STATUS");
        }
    } else {
        add_event(100U, "STATUS");
        add_event(200U, "MOVE 1000");
        add_event(210U, "MOVE bad");
        add_event(300U, "HELP");
        add_event(400U, "MOVE_REL -25");
        add_event(500U, "SPEED 500");
        add_event(600U, "STOP");
        add_event(700U, "ESTOP");
        add_event(800U, "RESET");
        add_event(900U, "HOME");
        if (scenario == UART_LONG) {
            host_input_event_t *event = &events[event_count++];
            event->at_ms = 1000U;
            event->line.length = UART_RX_LINE_CAPACITY;
            memset(event->line.text, 'X', sizeof(event->line.text));
        }
    }
}

static void harness_task(void *argument)
{
    (void)argument;
    if (scenario == PERIPHERALS) {
        vTaskDelay(pdMS_TO_TICKS(200U));
        injection_ok = encoder_host_set_count(1000) && gpio_host_set_estop(true) &&
                       gpio_host_set_negative_limit(true) && gpio_host_set_positive_limit(true);
        vTaskDelay(pdMS_TO_TICKS(1000U));
    } else { vTaskDelay(pdMS_TO_TICKS(1200U)); }
    runtime_snapshot(&final_snapshot);
    host_input_stats(&final_input);
    host_input_stop();
    runtime_request_stop();
    /* Let tasks park outside work and stdio before returning to main. */
    for (unsigned int attempt = 0U; attempt < 30U; ++attempt) {
        runtime_snapshot_t snapshot;
        runtime_snapshot(&snapshot);
        if (snapshot.tasks_parked == RUNTIME_ALL_TASKS) { parked = true; break; }
        vTaskDelay(1U);
    }
    completed = true;
    vTaskEndScheduler();
    /* Windows returns to main on its interrupt thread. No RTOS calls after end. */
    for (;;) { }
}

static bool validate(void)
{
    const bool seeking_home = scenario != PERIPHERALS && scenario != BURST && scenario != RX_OVERFLOW;
    if (!completed || !parked || !injection_ok || host_motor_enabled() ||
        !final_snapshot.encoder_valid || !final_snapshot.gpio_valid || final_snapshot.motor_output != (seeking_home ? HOMING_PWM : 0.0f) ||
        final_snapshot.uart_rx_errors != 0U || !final_watchdog.initialized || final_watchdog.refresh_count == 0U ||
        final_snapshot.state != (scenario == PERIPHERALS ? APP_ESTOP : (seeking_home ? APP_HOMING : APP_IDLE)) || final_snapshot.tasks_started != RUNTIME_ALL_TASKS ||
        final_snapshot.queue_used != 0U || final_snapshot.queued != final_snapshot.consumed ||
        final_snapshot.telemetry_cycles < 8U || final_snapshot.motion_min_gap_ms == 0U ||
        final_snapshot.health[HEALTH_MOTION].count < 80U ||
        final_snapshot.health[HEALTH_SAFETY].count < 40U ||
        final_snapshot.health[HEALTH_COMMS].count < 10U ||
        final_snapshot.input_notifications < final_input.irq_notifications || final_input.irq_notifications == 0U ||
        final_input.released != event_count ||
        final_input.accepted + final_input.dropped != final_input.released ||
        final_input.accepted != final_snapshot.queued + final_snapshot.rejected + final_snapshot.parse_errors) { return false; }
    for (unsigned int i = 0U; i < HEALTH_COUNT; ++i) {
        if (!task_health_fresh(&final_snapshot.health[i], final_snapshot.timestamp_ms, HEALTH_MAXIMUM_AGE_MS)) { return false; }
    }
    if (scenario != BURST && scenario != RX_OVERFLOW) {
        if (final_snapshot.consumed != 9U || final_snapshot.parse_errors != (scenario == UART_LONG ? 2U : 1U) ||
            final_snapshot.rejected != 0U || final_input.dropped != 0U) { return false; }
        for (unsigned int i = 0U; i <= CMD_HELP; ++i) {
            if (final_snapshot.received_by_type[i] != 1U) { return false; }
        }
        if (scenario == DIAGNOSTIC_LOSS && final_snapshot.diagnostic_drops == 0U) { return false; }
    } else if (scenario == BURST) {
        if (final_snapshot.rejected == 0U || final_snapshot.queue_high_water != 8U || final_input.dropped != 0U) { return false; }
    } else if (final_input.dropped == 0U) { return false; }
    if (scenario == PERIPHERALS && (final_snapshot.encoder_count != 1000 || !final_snapshot.gpio.estop ||
                                   !final_snapshot.gpio.negative_limit || !final_snapshot.gpio.positive_limit)) { return false; }
    if (scenario == UART_TX_FAILURE) {
        if (final_uart.tx_errors == 0U || final_uart.tx_calls != 0U) { return false; }
    } else if (final_uart.tx_errors != 0U) { return false; }
    return true;
}

int main(int argc, char **argv)
{
    const char *scenario_name = "nominal";
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--quiet") == 0) { quiet = true; }
        else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) { scenario_name = argv[++i]; }
        else { fprintf(stderr, "Usage: %s [--quiet] [--scenario nominal|burst|rx-overflow|diagnostic-loss|peripherals|uart-long|uart-tx-failure]\n", argv[0]); return EXIT_FAILURE; }
    }
    if (strcmp(scenario_name, "nominal") == 0) { scenario = NOMINAL; }
    else if (strcmp(scenario_name, "burst") == 0) { scenario = BURST; }
    else if (strcmp(scenario_name, "rx-overflow") == 0) { scenario = RX_OVERFLOW; }
    else if (strcmp(scenario_name, "diagnostic-loss") == 0) { scenario = DIAGNOSTIC_LOSS; }
    else if (strcmp(scenario_name, "peripherals") == 0) { scenario = PERIPHERALS; }
    else if (strcmp(scenario_name, "uart-long") == 0) { scenario = UART_LONG; }
    else if (strcmp(scenario_name, "uart-tx-failure") == 0) { scenario = UART_TX_FAILURE; }
    else { fputs("Unknown scenario\n", stderr); return EXIT_FAILURE; }

    if (!platform_init() || !runtime_init()) { return EXIT_FAILURE; }
    if (!timer_host_set_source(rtos_clock_ms, (uint32_t)portTICK_PERIOD_MS) ||
        !host_access_configure(lock_peripherals, unlock_peripherals)) { return EXIT_FAILURE; }
    host_set_log_sink(diagnostics_text);
    host_telemetry_quiet(quiet && scenario != UART_TX_FAILURE);
    if (scenario == UART_TX_FAILURE && !uart_host_set_tx_sink(failing_tx_sink)) { return EXIT_FAILURE; }
    prepare_script();
    if (!host_input_init(events, event_count, runtime_input_ready_from_isr)) { return EXIT_FAILURE; }
    rtos_set_tick_callback(host_input_tick_isr);
    if (scenario == DIAGNOSTIC_LOSS) {
        for (unsigned int i = 0U; i < DIAGNOSTIC_CAPACITY + 8U; ++i) {
            (void)diagnostics_text("HOST injected diagnostic backlog");
        }
    }
    if (xTaskCreateStatic(harness_task, "HostHarness", configMINIMAL_STACK_SIZE,
                         NULL, TELEMETRY_PRIORITY, harness_stack, &harness_control) == NULL) { return EXIT_FAILURE; }
    printf("FreeRTOS %s host scenario=%s tick_hz=%u motion_ms=%u safety_ms=%u telemetry_ms=%u\n",
           tskKERNEL_VERSION_NUMBER, scenario_name, configTICK_RATE_HZ, MOTION_PERIOD_MS, SAFETY_PERIOD_MS, TELEMETRY_PERIOD_MS);
    fflush(stdout);
    vTaskStartScheduler();
    /* No firmware task can access peripheral state after cooperative parking. */
    (void)host_access_configure(NULL, NULL);
    if (!uart_host_get_stats(&final_uart) || !watchdog_host_get_state(&final_watchdog)) { return EXIT_FAILURE; }
    const bool passed = validate();
    printf("%s scenario=%s state=%s tasks=0x%" PRIx32 " queued=%" PRIu32 " consumed=%" PRIu32
           " rejected=%" PRIu32 " rx_dropped=%" PRIu32 " parse_errors=%" PRIu32
           " notifications=%" PRIu32 " irq_notifications=%" PRIu32 " high_water=%" PRIu32 " diag_dropped=%" PRIu32 "\n",
           passed ? "PHASE3_OK" : "PHASE3_FAILED", scenario_name, application_state_name(final_snapshot.state),
           final_snapshot.tasks_started, final_snapshot.queued, final_snapshot.consumed, final_snapshot.rejected,
           final_input.dropped, final_snapshot.parse_errors, final_snapshot.input_notifications,
           final_input.irq_notifications, final_snapshot.queue_high_water, final_snapshot.diagnostic_drops);
    printf("RTOS_TICKS motion_gap_ms=%" PRIu32 "..%" PRIu32 " overruns=%" PRIu32
           " heartbeats=%" PRIu32 "/%" PRIu32 "/%" PRIu32 " telemetry_cycles=%" PRIu32
           " motor_enabled=0 (not wall-clock or MCU timing)\n",
           final_snapshot.motion_min_gap_ms, final_snapshot.motion_max_gap_ms, final_snapshot.motion_overruns,
           final_snapshot.health[0].count, final_snapshot.health[1].count, final_snapshot.health[2].count,
           final_snapshot.telemetry_cycles);
    printf("PERIPHERALS encoder=%" PRId32 " gpio=%u/%u/%u pwm=%.2f watchdog_refreshes=%" PRIu32
           " timer_resolution_ms=%" PRIu32 " uart_tx_bytes=%" PRIu32 " uart_tx_errors=%" PRIu32 "\n",
           final_snapshot.encoder_count, final_snapshot.gpio.estop ? 1U : 0U,
           final_snapshot.gpio.negative_limit ? 1U : 0U, final_snapshot.gpio.positive_limit ? 1U : 0U,
           (double)final_snapshot.motor_output, final_watchdog.refresh_count, timer_resolution_ms(),
           final_uart.tx_bytes, final_uart.tx_errors);
    return passed && fflush(stdout) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
