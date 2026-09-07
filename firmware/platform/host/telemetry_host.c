#include "drivers/telemetry.h"
#include "platform/host/telemetry_host.h"
#include "drivers/uart.h"
#include "control/homing.h"
#include "rtos/command_bus.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>

static bool silent;

static void uart_print(const char *format, ...)
{
    char text[UART_TX_MAX_WRITE + 1U];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    /* Oversize output is rejected by UART, never sent as a truncated success. */
    if (length >= 0) { (void)uart_write((const uint8_t *)text, (size_t)length); }
}

/* Every line in a group comes from this one immutable copy. Formatting and TX
 * occur outside the runtime critical section. %g bounds even extreme floats. */
static void emit_snapshot(const char *kind, const runtime_snapshot_t *v)
{
    const safety_manager_t *s = &v->safety;
    const motion_snapshot_t *m = &v->motion;
    bool fresh[HEALTH_COUNT];
    bool healthy = true;
    for (unsigned int i = 0U; i < HEALTH_COUNT; ++i) {
        fresh[i] = task_health_fresh(&v->health[i], v->timestamp_ms, HEALTH_MAXIMUM_AGE_MS);
        healthy = healthy && fresh[i];
    }
    uart_print("%s t_ms=%" PRIu32 " motion state=%s target_counts=%" PRId32 " position_counts=%" PRId32
               " velocity_counts_s=%.6g error_counts=%.6g feedback_valid=%u\n",
               kind, v->timestamp_ms, application_state_name(v->state), m->target, m->position,
               (double)m->velocity, (double)m->error, m->feedback_valid ? 1U : 0U);
    uart_print("%s t_ms=%" PRIu32 " safety requested_pwm=%.6g applied_pwm=%.6g fault=%s fault_ms=%" PRIu32
               " estop=%u limit_neg=%u limit_pos=%u inputs_valid=%u\n",
               kind, v->timestamp_ms, (double)s->requested_pwm, (double)s->applied_pwm,
               fault_name(s->faults.active.code), s->faults.active.timestamp_ms,
               v->gpio.estop ? 1U : 0U, v->gpio.negative_limit ? 1U : 0U,
               v->gpio.positive_limit ? 1U : 0U, v->gpio_valid ? 1U : 0U);
    uart_print("%s t_ms=%" PRIu32 " health motion_fresh=%u safety_fresh=%u comms_fresh=%u watchdog_supervision=%s refreshes=%" PRIu32 "\n",
               kind, v->timestamp_ms, fresh[HEALTH_MOTION] ? 1U : 0U,
               fresh[HEALTH_SAFETY] ? 1U : 0U, fresh[HEALTH_COMMS] ? 1U : 0U,
               healthy ? "HEALTHY" : "UNHEALTHY", s->watchdog_refreshes);
    uart_print("%s t_ms=%" PRIu32 " home result=%s referenced=%u elapsed_ms=%" PRIu32
               " completed=%" PRIu32 " contact_allowance=%u\n",
               kind, v->timestamp_ms, homing_result_name(m->homing.result), m->homing.referenced ? 1U : 0U,
               m->homing.elapsed_ms, m->homing.completed, s->home_contact ? 1U : 0U);
    uart_print("%s t_ms=%" PRIu32 " counters queue_used=%" PRIu32 " queue_capacity=%u queue_high_water=%" PRIu32
               " diagnostic_drops=%" PRIu32 " moves_completed=%" PRIu32 " aborted=%" PRIu32 "\n",
               kind, v->timestamp_ms, v->queue_used, COMMAND_QUEUE_CAPACITY, v->queue_high_water,
               v->diagnostic_drops, m->completed_moves, s->aborted_moves);
}

void host_telemetry_quiet(bool quiet) { silent = quiet; }

void platform_diagnostic_emit(const diagnostic_t *record)
{
    if (silent) { return; }
    uart_print("[%06" PRIu32 "] ", record->timestamp_ms);
    switch (record->kind) {
    case DIAG_TEXT:
        uart_print("%s\n", record->text);
        break;
    case DIAG_QUEUED:
        uart_print("ACK QUEUED command=%s value=%" PRId32 "\n", command_name(record->command.type), record->command.value);
        break;
    case DIAG_QUEUE_FULL:
        uart_print("ERR QUEUE_FULL command=%s\n", command_name(record->command.type));
        break;
    case DIAG_PARSE_ERROR:
        uart_print("ERR %s\n", command_parse_error(record->parse_result));
        break;
    case DIAG_RECEIVED:
        if (record->command.type == CMD_STATUS) {
            runtime_snapshot_t snapshot;
            runtime_snapshot(&snapshot);
            uart_print("ACK STATUS snapshot_ms=%" PRIu32 "\n", snapshot.timestamp_ms);
            emit_snapshot("STATUS", &snapshot);
        } else if (record->command.type == CMD_HELP) {
            uart_print("ACK HELP\n");
            uart_print("HELP MOVE <position_counts> : absolute position move\n");
            uart_print("HELP MOVE_REL <offset_counts> : move relative to sampled position\n");
            uart_print("HELP HOME : seek negative switch and establish logical zero\n");
            uart_print("HELP STOP : remove drive and wait for rest\n");
            uart_print("HELP ESTOP : latch stop when this queued command executes\n");
            uart_print("HELP RESET : validate safety conditions and clear recoverable latch\n");
            uart_print("HELP STATUS : coherent snapshot at telemetry service time\n");
            uart_print("HELP HELP : command syntax; SPEED <integer> : deferred, NOT_IMPLEMENTED\n");
        } else { uart_print("ERR DIAGNOSTIC_KIND\n"); }
        break;
    case DIAG_MOTION:
        uart_print("MOTION command=%s value=%" PRId32 " %s\n", command_name(record->command.type),
                   record->command.value, motion_result_name(record->motion_result));
        break;
    default:
        uart_print("ERR DIAGNOSTIC_KIND\n");
        break;
    }
}

void platform_telemetry_emit(const runtime_snapshot_t *snapshot)
{
    if (!silent) { emit_snapshot("TEL", snapshot); }
}
