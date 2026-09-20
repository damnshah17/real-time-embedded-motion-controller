#include "drivers/telemetry.h"
#include "drivers/uart.h"
#include "platform/host/uart_host.h"
#include "test_check.h"
#include <float.h>
#include <string.h>
static char output[8192];
static size_t used, calls;
static runtime_snapshot_t fixture;
static bool rejected;
static bool sink(const uint8_t *data, size_t length)
{
    if (length > UART_TX_MAX_WRITE || used + length >= sizeof(output)) { rejected = true; return false; }
    memcpy(output + used, data, length); used += length; output[used] = '\0'; return true;
}
void runtime_snapshot(runtime_snapshot_t *snapshot) { *snapshot = fixture; ++calls; ++fixture.timestamp_ms; }
static void clear(void) { used = 0U; output[0] = '\0'; rejected = false; }
int main(void)
{
    const uart_config_t config = {115200U};
    CHECK(uart_init(&config) == UART_OK && uart_host_set_tx_sink(sink));
    fixture.timestamp_ms = UINT32_MAX;
    fixture.state = APP_STOPPING;
    fixture.motion.target = INT32_MIN; fixture.motion.position = INT32_MAX;
    fixture.motion.velocity = -FLT_MAX; fixture.motion.error = FLT_MAX;
    fixture.safety.requested_pwm = -1.0f; fixture.safety.applied_pwm = 1.0f;
    fixture.safety.faults.active.timestamp_ms = UINT32_MAX;
    fixture.queue_high_water = UINT32_MAX; fixture.diagnostic_drops = UINT32_MAX;
    fixture.motion.completed_moves = UINT32_MAX; fixture.safety.aborted_moves = UINT32_MAX;
    diagnostic_t d = {.kind = DIAG_RECEIVED, .command = {CMD_STATUS, 0}};
    platform_diagnostic_emit(&d);
    CHECK(calls == 1U && !rejected);
    CHECK(strstr(output, "ACK STATUS snapshot_ms=4294967295"));
    CHECK(strstr(output, "target_counts=-2147483648 position_counts=2147483647"));
    CHECK(strstr(output, "velocity_counts_s=-3.40282e+38"));
    CHECK(strstr(output, "requested_pwm=-1.00000e+00 applied_pwm=1.00000e+00"));
    CHECK(strstr(output, "STATUS t_ms=4294967295 counters"));
    CHECK(strstr(output, "aborted=4294967295"));
    clear(); platform_telemetry_emit(&fixture);
    CHECK(!rejected && strstr(output, "TEL t_ms=0 motion") && strstr(output, "TEL t_ms=0 home"));
    clear(); d.command.type = CMD_HELP; platform_diagnostic_emit(&d);
    CHECK(!rejected && strstr(output, "ACK HELP") && strstr(output, "SPEED <integer> : deferred, NOT_IMPLEMENTED"));
    clear(); d.kind = DIAG_QUEUED; d.command.type = CMD_MOVE_ABS; d.command.value = INT32_MIN;
    platform_diagnostic_emit(&d); CHECK(strstr(output,"ACK QUEUED command=MOVE value=-2147483648"));
    clear(); d.kind = DIAG_PARSE_ERROR; d.parse_result = PARSE_TOO_LONG;
    platform_diagnostic_emit(&d); CHECK(strstr(output,"ERR LINE_TOO_LONG"));
    clear(); d.kind = DIAG_MOTION; d.motion_result = MOTION_NOT_IMPLEMENTED;
    platform_diagnostic_emit(&d); CHECK(!rejected && strstr(output,"ERR NOT_IMPLEMENTED"));
    return 0;
}
