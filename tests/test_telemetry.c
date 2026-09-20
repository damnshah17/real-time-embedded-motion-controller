#include "drivers/telemetry.h"
#include "platform/host/telemetry_host.h"
#include "platform/host/uart_host.h"
#include "test_check.h"
#include <float.h>
#include <string.h>

static char output[8192];
static size_t used, calls;
static runtime_snapshot_t fixture;
static bool sink(const uint8_t *data, size_t length)
{
    if (length > UART_TX_MAX_WRITE || used + length >= sizeof(output)) { return false; }
    memcpy(output + used, data, length); used += length; output[used] = '\0';
    return true;
}
/* Formatter test supplies a changing source: STATUS must read it exactly once. */
void runtime_snapshot(runtime_snapshot_t *snapshot)
{
    *snapshot = fixture; ++calls; ++fixture.timestamp_ms;
}
static void clear(void) { used = 0U; output[0] = '\0'; }

int main(void)
{
    const uart_config_t config = {115200U};
    CHECK(uart_init(&config) == UART_OK && uart_host_set_tx_sink(sink));
    fixture.timestamp_ms = 1000U;
    fixture.motion.target = 2000; fixture.motion.position = 1234;
    fixture.motion.velocity = -25.0f; fixture.motion.feedback_valid = true;
    fixture.safety.requested_pwm = 0.8f; fixture.safety.applied_pwm = 0.0f;
    fixture.gpio.estop = true; fixture.gpio_valid = true;
    fixture.motion.homing.referenced = true;
    fixture.motion.homing.result = HOMING_COMPLETE;
    for (unsigned int i = 0U; i < HEALTH_COUNT; ++i) { task_health_beat(&fixture.health[i], 1000U); }
    const app_state_t states[] = {APP_IDLE, APP_MOVING, APP_STOPPING, APP_HOMING, APP_FAULT, APP_ESTOP};
    for (size_t i = 0U; i < sizeof(states) / sizeof(states[0]); ++i) {
        clear(); fixture.state = states[i]; platform_telemetry_emit(&fixture);
        CHECK(strstr(output, application_state_name(states[i])) != NULL);
        CHECK(strstr(output, "target_counts=2000 position_counts=1234 velocity_counts_s=-25") != NULL);
        CHECK(strstr(output, "requested_pwm=0.8 applied_pwm=0") != NULL);
        CHECK(strstr(output, "estop=1 limit_neg=0 limit_pos=0 inputs_valid=1") != NULL);
        CHECK(strstr(output, "watchdog_supervision=HEALTHY") != NULL);
        CHECK(strstr(output, "home result=COMPLETE referenced=1") != NULL);
        CHECK(strstr(output, "queue_capacity=8") != NULL);
        clear();
        diagnostic_t status = {.kind = DIAG_RECEIVED, .command = {CMD_STATUS, 0}};
        platform_diagnostic_emit(&status);
        CHECK(strstr(output, "ACK STATUS snapshot_ms=") != NULL);
        CHECK(strstr(output, "STATUS t_ms=") != NULL);
        CHECK(strstr(output, application_state_name(states[i])) != NULL);
        CHECK(strstr(output, "target_counts=2000 position_counts=1234") != NULL);
        CHECK(strstr(output, "requested_pwm=0.8 applied_pwm=0") != NULL);
    }
    clear(); calls = 0U; fixture.timestamp_ms = 1000U;
    diagnostic_t d = {.kind = DIAG_RECEIVED, .command = {CMD_STATUS, 0}};
    platform_diagnostic_emit(&d);
    CHECK(calls == 1U && strstr(output, "ACK STATUS snapshot_ms=1000") != NULL);
    CHECK(strstr(output, "STATUS t_ms=1001") == NULL);
    CHECK(strstr(output, "STATUS t_ms=1000 counters") != NULL);
    clear(); d.command.type = CMD_HELP; platform_diagnostic_emit(&d);
    CHECK(strstr(output, "MOVE <position_counts>") && strstr(output, "MOVE_REL <offset_counts>"));
    CHECK(strstr(output, "SPEED <integer> : deferred, NOT_IMPLEMENTED"));
    clear(); d.kind = DIAG_MOTION; d.command.type = CMD_SET_SPEED;
    d.motion_result = MOTION_NOT_IMPLEMENTED; platform_diagnostic_emit(&d);
    CHECK(strstr(output, "command=SPEED value=0 ERR NOT_IMPLEMENTED"));
    CHECK(strstr(output, "phase=5") == NULL);
    const motion_result_t errors[] = {MOTION_INVALID_STATE, MOTION_OUT_OF_RANGE, MOTION_INPUT_ERROR};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        clear(); d.motion_result = errors[i]; platform_diagnostic_emit(&d);
        CHECK(strstr(output, motion_result_name(errors[i])) != NULL);
        CHECK(strstr(output, "ACK") == NULL);
    }
    clear(); d.kind = DIAG_QUEUED; d.command.type = CMD_MOVE_ABS; platform_diagnostic_emit(&d);
    CHECK(strstr(output, "ACK QUEUED") && !strstr(output, "COMPLETE"));
    clear(); d.kind = DIAG_MOTION; d.motion_result = MOTION_ACCEPTED; platform_diagnostic_emit(&d);
    CHECK(strstr(output, "ACK ACCEPTED") && !strstr(output, "COMPLETE"));
    clear(); fixture.timestamp_ms = UINT32_MAX;
    fixture.motion.target = INT32_MIN; fixture.motion.position = INT32_MAX;
    fixture.motion.velocity = -FLT_MAX; fixture.motion.error = FLT_MAX;
    fixture.safety.faults.active.code = FAULT_NO_MOTION_UNDER_COMMAND;
    fixture.safety.faults.active.timestamp_ms = UINT32_MAX;
    fixture.safety.requested_pwm = -1.0f; fixture.safety.applied_pwm = -1.0f;
    platform_telemetry_emit(&fixture);
    CHECK(strstr(output, "fault=NO_MOTION_UNDER_COMMAND") != NULL);
    CHECK(strstr(output, "watchdog_supervision=UNHEALTHY") != NULL);
    CHECK(strstr(output, "TEL t_ms=4294967295 counters") != NULL);
    uart_host_stats_t stats;
    CHECK(uart_host_get_stats(&stats) && stats.tx_errors == 0U);
    clear(); host_telemetry_quiet(true); platform_telemetry_emit(&fixture); platform_diagnostic_emit(&d);
    CHECK(used == 0U);
    return EXIT_SUCCESS;
}
