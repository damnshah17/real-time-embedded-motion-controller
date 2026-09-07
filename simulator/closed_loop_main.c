#include "FreeRTOS.h"
#include "task.h"
#include "rtos/runtime.h"
#include "rtos/diagnostics.h"
#include "rtos/command_bus.h"
#include "drivers/platform.h"
#include "drivers/motor.h"
#include "drivers/encoder.h"
#include "control/homing.h"
#include "platform/host/platform_host.h"
#include "platform/host/host_access.h"
#include "platform/host/input_host.h"
#include "platform/host/timer_host.h"
#include "platform/host/telemetry_host.h"
#include "platform/host/gpio_host.h"
#include "platform/host/encoder_host.h"
#include "platform/host/watchdog_host.h"
#include "platform/host/uart_host.h"
#include "plant_model.h"
#include "control_metrics.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_MOVES = 4, MAX_TRACE = 64, MAX_MOVE_CYCLES = 2000, POST_CYCLES = 200 };
typedef struct {
    control_metrics_t metrics;
    motion_snapshot_t trace[MAX_TRACE];
    uint32_t trace_ms[MAX_TRACE];
    size_t trace_count;
} move_record_t;
static move_record_t records[MAX_MOVES];
static size_t record_count;
static plant_model_t plant;
static runtime_snapshot_t snapshot;
static StaticTask_t harness_control;
static StackType_t harness_stack[configMINIMAL_STACK_SIZE * 2U];
static bool passed, parked;
static const char *scenario = "sequence";
static unsigned int failure_line;
static uint32_t dropped_heartbeats;
static uint32_t injection_ms, detection_ms, safe_ms;
static bool heartbeat_filter(task_health_id_t id) { return (dropped_heartbeats & (1U << id)) == 0U; }
static bool failed_sink(const uint8_t *bytes, size_t length) { (void)bytes; (void)length; return false; }
static runtime_snapshot_t safety_trace[64];
static size_t safety_trace_count;
typedef struct {
    int32_t start_logical, start_raw, final_logical, final_raw, reference_offset;
    double physical_end;
    uint32_t elapsed_ms;
    homing_result_t result;
    app_state_t state;
    float pwm;
} home_record_t;
static home_record_t home_records[4];
static size_t home_count;
static bool known_homing_scenario(void)
{
    static const char *names[] = {"homing", "homing-500", "homing-4000", "homing-reference", "homing-already",
        "homing-after-move", "homing-departure", "homing-stop", "homing-estop", "homing-software-estop",
        "homing-positive-limit", "homing-watchdog", "homing-stall", "homing-encoder-failure", "homing-timeout"};
    for (size_t i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) { if (strcmp(scenario, names[i]) == 0) { return true; } }
    return false;
}
static void drop_heartbeats(uint32_t mask)
{ taskENTER_CRITICAL(); dropped_heartbeats = mask; taskEXIT_CRITICAL(); }
static void lock(void) { taskENTER_CRITICAL(); }
static void unlock(void) { taskEXIT_CRITICAL(); }
#define REQUIRE(c) do { if (!(c)) { failure_line = __LINE__; return false; } } while (0)

static bool cycle(void)
{
    for (uint32_t i = 0U; i < MOTION_PERIOD_MS / plant.config.step_ms; ++i) {
        REQUIRE(plant_step(&plant));
        REQUIRE(timer_host_advance_ms(plant.config.step_ms));
    }
    runtime_input_ready();
    if (platform_time_ms() % SAFETY_PERIOD_MS == 0U) {
        REQUIRE(runtime_step_safety());
        /* Check the final gate before Motion gets its next release. */
        if (motor_is_inhibited()) { REQUIRE(motor_get_commanded_output() == 0.0f); }
    }
    REQUIRE(runtime_step_motion());
    runtime_snapshot(&snapshot);
    if (safety_trace_count < 64U && (safety_trace_count == 0U ||
        safety_trace[safety_trace_count - 1U].state != snapshot.state ||
        safety_trace[safety_trace_count - 1U].safety.candidate != snapshot.safety.candidate ||
        safety_trace[safety_trace_count - 1U].gpio.estop != snapshot.gpio.estop ||
        safety_trace[safety_trace_count - 1U].gpio.negative_limit != snapshot.gpio.negative_limit ||
        safety_trace[safety_trace_count - 1U].safety.home_contact != snapshot.safety.home_contact ||
        safety_trace[safety_trace_count - 1U].motion.homing.result != snapshot.motion.homing.result)) {
        safety_trace[safety_trace_count++] = snapshot;
    }
    REQUIRE(snapshot.control_errors == 0U && snapshot.encoder_valid);
    REQUIRE(fabsf(snapshot.motion.pwm) <= motion_default_config().pid.output_max);
    return true;
}
static bool send_command(const char *text)
{
    input_line_t line = {.length = strlen(text)};
    REQUIRE(line.length < sizeof(line.text));
    memcpy(line.text, text, line.length);
    const uint32_t consumed = snapshot.consumed;
    REQUIRE(host_input_submit(&line));
    runtime_input_ready(); /* Higher-priority Comms parses/enqueues before we resume. */
    REQUIRE(cycle());
    REQUIRE(snapshot.consumed == consumed + 1U);
    return true;
}
static void record_sample(move_record_t *r, uint32_t elapsed_ms)
{
    const motion_config_t config = motion_default_config();
    control_metrics_sample(&r->metrics, &snapshot.motion, &config, elapsed_ms);
    if ((elapsed_ms % 500U == 0U || snapshot.state == APP_IDLE) && r->trace_count < MAX_TRACE &&
        (!r->metrics.completed || elapsed_ms == r->metrics.completion_ms)) {
        r->trace[r->trace_count] = snapshot.motion;
        r->trace_ms[r->trace_count++] = elapsed_ms;
    }
}
static bool move(const char *command, int32_t target)
{
    REQUIRE(record_count < MAX_MOVES && snapshot.state == APP_IDLE);
    const int32_t start = snapshot.motion.position;
    const uint32_t completed = snapshot.motion.completed_moves;
    REQUIRE(send_command(command));
    REQUIRE(snapshot.last_motion_result == MOTION_ACCEPTED && snapshot.state == APP_MOVING);
    REQUIRE(snapshot.motion.target == target && snapshot.motion.completed_moves == completed);
    move_record_t *r = &records[record_count++];
    control_metrics_init(&r->metrics, start, target);
    record_sample(r, 0U);
    uint32_t cycles = 0U;
    while (snapshot.state == APP_MOVING && cycles < MAX_MOVE_CYCLES) {
        REQUIRE(cycle());
        ++cycles;
        record_sample(r, cycles * MOTION_PERIOD_MS);
        REQUIRE(!plant.state.negative_limit && !plant.state.positive_limit);
    }
    REQUIRE(snapshot.state == APP_IDLE && snapshot.motion.completed_moves == completed + 1U);
    REQUIRE(snapshot.motion.pwm == 0.0f && motor_get_commanded_output() == 0.0f);
    for (unsigned int i = 0U; i < POST_CYCLES; ++i) {
        REQUIRE(cycle());
        record_sample(r, ++cycles * MOTION_PERIOD_MS);
        REQUIRE(snapshot.state == APP_IDLE && snapshot.motion.pwm == 0.0f);
        REQUIRE(fabsf(snapshot.motion.error) <= motion_default_config().tolerance);
    }
    REQUIRE(r->metrics.completed && r->metrics.in_band);
    REQUIRE(r->metrics.final_error <= motion_default_config().tolerance);
    return true;
}
static bool policy(void)
{
    REQUIRE(send_command("MOVE -1") && snapshot.last_motion_result == MOTION_OUT_OF_RANGE);
    REQUIRE(send_command("MOVE 5001") && snapshot.last_motion_result == MOTION_OUT_OF_RANGE);
    REQUIRE(send_command("MOVE_REL 2147483647") && snapshot.last_motion_result == MOTION_OUT_OF_RANGE);
    REQUIRE(snapshot.state == APP_IDLE && snapshot.motion.pwm == 0.0f);
    REQUIRE(send_command("MOVE 2000") && snapshot.last_motion_result == MOTION_ACCEPTED);
    REQUIRE(send_command("MOVE 500") && snapshot.last_motion_result == MOTION_INVALID_STATE);
    REQUIRE(snapshot.motion.target == 2000);
    REQUIRE(send_command("MOVE_REL -100") && snapshot.last_motion_result == MOTION_INVALID_STATE);
    REQUIRE(send_command("SPEED 500") && snapshot.last_motion_result == MOTION_NOT_IMPLEMENTED);
    REQUIRE(snapshot.state == APP_MOVING && snapshot.motion.target == 2000);
    REQUIRE(send_command("STOP") && snapshot.last_motion_result == MOTION_ACCEPTED);
    REQUIRE(snapshot.state == APP_STOPPING && snapshot.motion.pwm == 0.0f);
    for (unsigned int i = 0U; i < 1000U && snapshot.state == APP_STOPPING; ++i) { REQUIRE(cycle()); }
    REQUIRE(snapshot.state == APP_IDLE && snapshot.motion.completed_moves == 0U);
    return true;
}
static bool stop_scenario(void)
{
    REQUIRE(send_command("MOVE 3500"));
    for (unsigned int i = 0U; i < 100U; ++i) { REQUIRE(cycle()); }
    REQUIRE(send_command("STOP"));
    const double at_stop = plant.state.position;
    REQUIRE(snapshot.state == APP_STOPPING && snapshot.motion.pwm == 0.0f && plant.state.velocity > 0.0);
    for (unsigned int i = 0U; i < 1000U && snapshot.state == APP_STOPPING; ++i) {
        REQUIRE(cycle()); REQUIRE(snapshot.motion.pwm == 0.0f);
    }
    REQUIRE(snapshot.state == APP_IDLE && plant.state.position > at_stop && snapshot.motion.completed_moves == 0U);
    return true;
}
static bool safe_rejections(app_state_t state)
{
    const char *commands[] = {"MOVE 2000", "MOVE_REL -10", "SPEED 500", "HOME"};
    for (size_t i = 0U; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        REQUIRE(send_command(commands[i]) && snapshot.last_motion_result == MOTION_INVALID_STATE);
        REQUIRE(snapshot.state == state && motor_get_commanded_output() == 0.0f);
    }
    REQUIRE(send_command("STATUS") && send_command("HELP") && send_command("STOP"));
    REQUIRE(snapshot.state == state && snapshot.last_motion_result == MOTION_ACCEPTED);
    REQUIRE(motor_set_output(0.8f) == MOTOR_INHIBITED && motor_get_commanded_output() == 0.0f);
    REQUIRE(motor_set_output(-0.8f) == MOTOR_INHIBITED && motor_get_commanded_output() == 0.0f);
    return true;
}
static bool recover(void)
{
    REQUIRE(send_command("RESET") && snapshot.last_motion_result == MOTION_ACCEPTED);
    REQUIRE(snapshot.state == APP_IDLE && snapshot.motion.pwm == 0.0f && !motor_is_inhibited());
    for (unsigned int i = 0U; i < 200U; ++i) {
        REQUIRE(cycle() && snapshot.state == APP_IDLE && motor_get_commanded_output() == 0.0f);
    }
    return move("MOVE 2000", 2000);
}
static bool estop_scenario(bool idle)
{
    if (!idle) {
        REQUIRE(send_command("MOVE 4000"));
        for (unsigned int i = 0U; i < 50U; ++i) { REQUIRE(cycle()); }
        REQUIRE(motor_get_commanded_output() > 0.0f);
    }
    injection_ms = platform_time_ms();
    REQUIRE(gpio_host_set_estop(true));
    for (unsigned int i = 0U; i < 2U && snapshot.state != APP_ESTOP; ++i) { REQUIRE(cycle()); }
    REQUIRE(snapshot.state == APP_ESTOP && motor_get_commanded_output() == 0.0f);
    detection_ms = snapshot.safety.detection_ms; safe_ms = snapshot.safety.zero_ms;
    REQUIRE(detection_ms - injection_ms <= SAFETY_PERIOD_MS && safe_ms == detection_ms);
    REQUIRE(snapshot.motion.completed_moves == 0U && snapshot.safety.aborted_moves == (idle ? 0U : 1U));
    REQUIRE(safe_rejections(APP_ESTOP));
    REQUIRE(send_command("RESET") && snapshot.last_motion_result == MOTION_INVALID_STATE);
    REQUIRE(gpio_host_set_estop(false));
    REQUIRE(cycle() && cycle() && snapshot.state == APP_ESTOP);
    REQUIRE(recover());
    REQUIRE(send_command("MOVE 4000") && snapshot.state == APP_MOVING);
    REQUIRE(send_command("ESTOP") && snapshot.state == APP_ESTOP && motor_get_commanded_output() == 0.0f);
    REQUIRE(send_command("STOP") && snapshot.state == APP_ESTOP);
    REQUIRE(send_command("RESET") && snapshot.state == APP_IDLE && motor_get_commanded_output() == 0.0f);
    return true;
}
static bool no_motion_scenario(bool freeze)
{
    REQUIRE(send_command("MOVE 4000"));
    for (unsigned int i = 0U; i < 50U; ++i) { REQUIRE(cycle()); }
    injection_ms = platform_time_ms();
    const double position = plant.state.position;
    const int32_t count = snapshot.motion.position;
    plant.stalled = !freeze; plant.encoder_frozen = freeze;
    for (unsigned int i = 0U; i < 100U && snapshot.state == APP_MOVING; ++i) { REQUIRE(cycle()); }
    REQUIRE(snapshot.state == APP_FAULT && snapshot.safety.faults.active.code == FAULT_NO_MOTION_UNDER_COMMAND);
    detection_ms = snapshot.safety.detection_ms; safe_ms = snapshot.safety.zero_ms;
    REQUIRE(detection_ms - injection_ms >= SAFETY_STALL_TIMEOUT_MS);
    REQUIRE(snapshot.motion.completed_moves == 0U && snapshot.safety.aborted_moves == 1U);
    REQUIRE(snapshot.motion.position == count && motor_get_commanded_output() == 0.0f);
    REQUIRE(freeze ? plant.state.position > position + 10.0 : plant.state.position == position);
    REQUIRE(safe_rejections(APP_FAULT));
    REQUIRE(send_command("RESET") && snapshot.last_motion_result == MOTION_INVALID_STATE);
    const uint32_t retained_ms = snapshot.safety.faults.active.timestamp_ms;
    plant.stalled = false; plant.encoder_frozen = false;
    if (!freeze) {
        REQUIRE(send_command("RESET") && snapshot.last_motion_result == MOTION_INVALID_STATE);
        /* Manual service displacement supplies firmware-visible recovery evidence. */
        REQUIRE(plant_reset(&plant, position + 10.0));
    }
    REQUIRE(cycle() && cycle() && snapshot.state == APP_FAULT);
    REQUIRE(snapshot.safety.faults.active.timestamp_ms == retained_ms);
    REQUIRE(recover());
    REQUIRE(snapshot.safety.faults.history.code == FAULT_NO_MOTION_UNDER_COMMAND);
    return true;
}
static bool limit_scenario(bool negative)
{
    REQUIRE(send_command(negative ? "MOVE 100" : "MOVE 4900"));
    for (unsigned int i = 0U; i < 30U; ++i) { REQUIRE(cycle()); }
    REQUIRE(plant_reset(&plant, negative ? 0.0 : 5000.0));
    /* Reset is host setup and disables drive; restore an outward stale request
     * so the test proves Safety actually removes nonzero output at the limit. */
    REQUIRE(motor_set_output(negative ? -0.8f : 0.8f) == MOTOR_OK);
    REQUIRE(cycle() && cycle());
    REQUIRE(snapshot.state == APP_FAULT && motor_get_commanded_output() == 0.0f);
    REQUIRE(snapshot.safety.faults.active.code == (negative ? FAULT_NEGATIVE_LIMIT : FAULT_POSITIVE_LIMIT));
    REQUIRE(snapshot.motion.completed_moves == 0U && snapshot.safety.aborted_moves == 1U);
    REQUIRE(safe_rejections(APP_FAULT));
    REQUIRE(send_command("RESET") && snapshot.last_motion_result == MOTION_INVALID_STATE);
    REQUIRE(plant_reset(&plant, 1000.0));
    REQUIRE(cycle() && cycle() && snapshot.state == APP_FAULT);
    return recover();
}
static bool watchdog_scenario(task_health_id_t id)
{
    REQUIRE(send_command("MOVE 4000"));
    for (unsigned int i = 0U; i < 30U; ++i) { REQUIRE(cycle()); }
    REQUIRE(snapshot.safety.watchdog_refreshes > 0U);
    injection_ms = platform_time_ms();
    drop_heartbeats(1U << id);
    for (unsigned int i = 0U; i < 30U && snapshot.state == APP_MOVING; ++i) { REQUIRE(cycle()); }
    REQUIRE(snapshot.state == APP_FAULT && snapshot.safety.faults.active.code == FAULT_WATCHDOG);
    detection_ms = snapshot.safety.detection_ms; safe_ms = snapshot.safety.zero_ms;
    REQUIRE(snapshot.safety.unhealthy_mask == (1U << id) && motor_get_commanded_output() == 0.0f);
    const uint32_t refreshes = snapshot.safety.watchdog_refreshes;
    REQUIRE(safe_rejections(APP_FAULT));
    REQUIRE(send_command("RESET") && snapshot.last_motion_result == MOTION_INVALID_STATE);
    REQUIRE(snapshot.safety.watchdog_refreshes == refreshes);
    drop_heartbeats(0U);
    REQUIRE(cycle() && cycle() && cycle() && snapshot.state == APP_FAULT);
    REQUIRE(snapshot.safety.healthy);
    return recover();
}
static bool start_home(void)
{
    REQUIRE(snapshot.state == APP_IDLE && home_count < 4U);
    home_record_t *r = &home_records[home_count++];
    REQUIRE(encoder_get_count(&r->start_logical));
    r->start_raw = plant.state.raw_encoder_count;
    REQUIRE(send_command("HOME") && snapshot.last_motion_result == MOTION_ACCEPTED);
    REQUIRE(snapshot.state == APP_HOMING || snapshot.motion.homing.result == HOMING_COMPLETE);
    return true;
}
static bool record_home(void)
{
    encoder_host_state_t encoder;
    REQUIRE(encoder_host_get_state(&encoder));
    home_record_t *r = &home_records[home_count - 1U];
    r->final_logical = snapshot.motion.position; r->final_raw = encoder.raw_count;
    r->reference_offset = encoder.reference_offset;
    r->physical_end = plant.state.position; r->elapsed_ms = snapshot.motion.homing.elapsed_ms;
    r->result = snapshot.motion.homing.result; r->state = snapshot.state;
    r->pwm = motor_get_commanded_output();
    return true;
}
static bool finish_home(void)
{
    for (unsigned int i = 0U; i <= HOMING_TIMEOUT_MS / MOTION_PERIOD_MS && snapshot.state == APP_HOMING; ++i) {
        REQUIRE(snapshot.motion.pwm == HOMING_PWM);
        REQUIRE(cycle());
    }
    REQUIRE(record_home());
    REQUIRE(snapshot.state == APP_IDLE && snapshot.motion.homing.result == HOMING_COMPLETE);
    REQUIRE(snapshot.motion.homing.referenced && snapshot.motion.position == 0 && snapshot.motion.target == 0);
    REQUIRE(snapshot.motion.pwm == 0.0f && plant.state.position == plant.config.min_position);
    REQUIRE(snapshot.safety.home_contact && snapshot.gpio.negative_limit && motor_is_inhibited());
    REQUIRE(motor_set_output(-0.8f) == MOTOR_INHIBITED && motor_get_commanded_output() == 0.0f);
    REQUIRE(motor_set_output(0.8f) == MOTOR_INHIBITED && motor_get_commanded_output() == 0.0f);
    motor_disable();
    for (unsigned int i = 0U; i < 10U; ++i) {
        REQUIRE(cycle() && snapshot.state == APP_IDLE && snapshot.motion.velocity == 0.0f);
        REQUIRE(motor_get_commanded_output() == 0.0f && snapshot.safety.home_contact);
    }
    return true;
}
static bool home_then_depart(void)
{
    REQUIRE(start_home() && finish_home());
    REQUIRE(send_command("MOVE_REL -10") && snapshot.last_motion_result == MOTION_INVALID_STATE);
    REQUIRE(send_command("MOVE 0") && snapshot.last_motion_result == MOTION_INVALID_STATE);
    REQUIRE(snapshot.state == APP_IDLE && motor_get_commanded_output() == 0.0f);
    REQUIRE(move("MOVE 500", 500));
    REQUIRE(!snapshot.safety.home_contact && !snapshot.gpio.negative_limit);
    REQUIRE(fabs(plant.state.position - (plant.config.min_position + 500.0)) <= 3.0);
    return true;
}
static bool homing_interruption(void)
{
    REQUIRE(start_home());
    for (unsigned int i = 0U; i < 100U; ++i) { REQUIRE(cycle()); }
    REQUIRE(snapshot.state == APP_HOMING && snapshot.motion.pwm == HOMING_PWM);
    const char *rejected[] = {"MOVE 500", "MOVE_REL 100", "HOME", "SPEED 100", "RESET"};
    for (size_t i = 0U; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        REQUIRE(send_command(rejected[i]) && snapshot.last_motion_result == MOTION_INVALID_STATE);
    }
    REQUIRE(send_command("STATUS") && send_command("HELP"));
    if (strcmp(scenario, "homing-stop") == 0) {
        REQUIRE(send_command("STOP") && snapshot.state == APP_STOPPING);
        REQUIRE(snapshot.motion.homing.result == HOMING_STOPPED && snapshot.motion.homing.completed == 0U);
        REQUIRE(record_home());
        REQUIRE(send_command("HOME") && snapshot.last_motion_result == MOTION_INVALID_STATE);
        for (unsigned int i = 0U; i < 1000U && snapshot.state == APP_STOPPING; ++i) { REQUIRE(cycle()); }
        REQUIRE(snapshot.state == APP_IDLE && snapshot.motion.position == plant.state.raw_encoder_count);
    } else if (strcmp(scenario, "homing-estop") == 0 || strcmp(scenario, "homing-software-estop") == 0) {
        if (strcmp(scenario, "homing-estop") == 0) {
            REQUIRE(gpio_host_set_estop(true) && cycle() && cycle());
            REQUIRE(send_command("RESET") && snapshot.last_motion_result == MOTION_INVALID_STATE);
            REQUIRE(gpio_host_set_estop(false));
        } else { REQUIRE(send_command("ESTOP")); }
        REQUIRE(snapshot.state == APP_ESTOP && snapshot.motion.homing.result == HOMING_ESTOPPED);
        REQUIRE(snapshot.motion.homing.completed == 0U && snapshot.motion.position == plant.state.raw_encoder_count);
        REQUIRE(record_home());
        REQUIRE(cycle() && cycle() && snapshot.state == APP_ESTOP);
        REQUIRE(send_command("RESET") && snapshot.state == APP_IDLE);
    } else {
        if (strcmp(scenario, "homing-positive-limit") == 0) {
            REQUIRE(plant_reset(&plant, plant.config.max_position));
            plant.stalled = true;
            REQUIRE(motor_set_output(HOMING_PWM) == MOTOR_OK);
        } else if (strcmp(scenario, "homing-watchdog") == 0) { drop_heartbeats(1U << HEALTH_MOTION); }
        else if (strcmp(scenario, "homing-stall") == 0) { plant.stalled = true; }
        else { plant.encoder_frozen = true; }
        for (unsigned int i = 0U; i < 100U && snapshot.state == APP_HOMING; ++i) { REQUIRE(cycle()); }
        REQUIRE(snapshot.state == APP_FAULT && snapshot.motion.homing.result == HOMING_FAULTED);
        const fault_code_t expected = strcmp(scenario, "homing-positive-limit") == 0 ? FAULT_POSITIVE_LIMIT :
            (strcmp(scenario, "homing-watchdog") == 0 ? FAULT_WATCHDOG : FAULT_NO_MOTION_UNDER_COMMAND);
        REQUIRE(snapshot.safety.faults.active.code == expected && snapshot.motion.homing.completed == 0U);
        REQUIRE(record_home());
        REQUIRE(send_command("RESET") && snapshot.last_motion_result == MOTION_INVALID_STATE);
        drop_heartbeats(0U);
        REQUIRE(plant_reset(&plant, 1000.0)); /* Service restores movement/feedback and clears inputs. */
        REQUIRE(cycle() && cycle() && cycle() && snapshot.state == APP_FAULT);
        REQUIRE(send_command("RESET") && snapshot.state == APP_IDLE);
    }
    REQUIRE(snapshot.motion.pwm == 0.0f && motor_get_commanded_output() == 0.0f);
    for (unsigned int i = 0U; i < 100U; ++i) { REQUIRE(cycle() && snapshot.state == APP_IDLE); }
    return home_then_depart();
}
static bool homing_scenario(void)
{
    if (strcmp(scenario, "homing-timeout") == 0) {
        REQUIRE(start_home());
        for (unsigned int i = 0U; i <= HOMING_TIMEOUT_MS / MOTION_PERIOD_MS && snapshot.state == APP_HOMING; ++i) { REQUIRE(cycle()); }
        REQUIRE(record_home());
        REQUIRE(snapshot.state == APP_FAULT && snapshot.safety.faults.active.code == FAULT_HOMING_TIMEOUT);
        REQUIRE(snapshot.motion.homing.result == HOMING_TIMEOUT && snapshot.motion.homing.elapsed_ms == HOMING_TIMEOUT_MS);
        REQUIRE(snapshot.motion.homing.completed == 0U && !snapshot.motion.homing.referenced && motor_get_commanded_output() == 0.0f);
        REQUIRE(safe_rejections(APP_FAULT));
        const plant_config_t restored = plant_default_config();
        REQUIRE(plant_init(&plant, &restored, 1000.0) && cycle() && cycle());
        REQUIRE(snapshot.state == APP_FAULT && snapshot.safety.faults.active.code == FAULT_HOMING_TIMEOUT);
        REQUIRE(send_command("RESET") && snapshot.state == APP_IDLE);
        return home_then_depart();
    }
    if (strcmp(scenario, "homing-after-move") == 0) {
        REQUIRE(move("MOVE 3000", 3000) && home_then_depart());
        return start_home() && finish_home();
    }
    if (strcmp(scenario, "homing-already") == 0) {
        /* Present contact while still IDLE, then HOME before the next Safety
         * release. An already latched limit FAULT cannot be bypassed by HOME. */
        REQUIRE(plant_reset(&plant, plant.config.min_position));
        REQUIRE(start_home() && finish_home());
        REQUIRE(start_home() && finish_home());
        REQUIRE(snapshot.motion.homing.completed == 2U && snapshot.motion.homing.elapsed_ms == 0U);
        return move("MOVE_REL 500", 500);
    }
    if (strcmp(scenario, "homing-departure") == 0) {
        REQUIRE(start_home() && finish_home());
        REQUIRE(send_command("MOVE_REL -1") && snapshot.last_motion_result == MOTION_INVALID_STATE);
        REQUIRE(send_command("MOVE_REL 500") && snapshot.last_motion_result == MOTION_ACCEPTED);
        REQUIRE(snapshot.state == APP_MOVING && snapshot.safety.home_contact && snapshot.motion.pwm > 0.0f);
        REQUIRE(motor_set_output(-0.8f) == MOTOR_INHIBITED && motor_get_commanded_output() == 0.0f);
        REQUIRE(cycle() && cycle() && cycle());
        REQUIRE(!snapshot.safety.home_contact && !snapshot.gpio.negative_limit);
        REQUIRE(plant_reset(&plant, plant.config.min_position));
        plant.stalled = true;
        REQUIRE(motor_set_output(-0.8f) == MOTOR_OK);
        REQUIRE(cycle() && cycle());
        REQUIRE(snapshot.state == APP_FAULT && snapshot.safety.faults.active.code == FAULT_NEGATIVE_LIMIT);
        REQUIRE(motor_get_commanded_output() == 0.0f);
        return true;
    }
    if (strcmp(scenario, "homing") == 0 || strcmp(scenario, "homing-500") == 0 ||
        strcmp(scenario, "homing-4000") == 0 || strcmp(scenario, "homing-reference") == 0) { return home_then_depart(); }
    return homing_interruption();
}
static bool run(void)
{
    REQUIRE(cycle() && cycle() && snapshot.state == APP_IDLE);
    if (strncmp(scenario, "homing", 6U) == 0) { return homing_scenario(); }
    if (strcmp(scenario, "estop") == 0) { return estop_scenario(false); }
    if (strcmp(scenario, "estop-telemetry") == 0) { return estop_scenario(false); }
    if (strcmp(scenario, "estop-idle") == 0) { return estop_scenario(true); }
    if (strcmp(scenario, "stall") == 0) { return no_motion_scenario(false); }
    if (strcmp(scenario, "encoder-failure") == 0) { return no_motion_scenario(true); }
    if (strcmp(scenario, "limit-fault") == 0) { return limit_scenario(false); }
    if (strcmp(scenario, "negative-limit") == 0) { return limit_scenario(true); }
    if (strcmp(scenario, "watchdog") == 0) { return watchdog_scenario(HEALTH_MOTION); }
    if (strcmp(scenario, "watchdog-safety") == 0) { return watchdog_scenario(HEALTH_SAFETY); }
    if (strcmp(scenario, "watchdog-comms") == 0) { return watchdog_scenario(HEALTH_COMMS); }
    if (strcmp(scenario, "positive") == 0) { return move("MOVE 2000", 2000); }
    if (strcmp(scenario, "negative") == 0) { return move("MOVE 500", 500); }
    if (strcmp(scenario, "relative") == 0) { return move("MOVE_REL -300", 700); }
    if (strcmp(scenario, "zero") == 0) {
        REQUIRE(move("MOVE 1000", 1000));
        REQUIRE(records[0].metrics.maximum_pwm == 0.0f && plant.state.position == 1000.0);
        return true;
    }
    if (strcmp(scenario, "policy") == 0) { return policy(); }
    if (strcmp(scenario, "stop") == 0) { return stop_scenario(); }
    return move("MOVE 2000", 2000) && move("MOVE 500", 500) &&
        move("MOVE 3500", 3500) && move("MOVE_REL -500", snapshot.motion.position - 500);
}
static void harness(void *argument)
{
    (void)argument;
    passed = run();
    host_input_stop();
    runtime_request_stop();
    for (unsigned int i = 0U; i < 30U; ++i) {
        runtime_snapshot(&snapshot);
        if (snapshot.tasks_parked == RUNTIME_ALL_TASKS) { parked = true; break; }
        vTaskDelay(1U);
    }
    vTaskEndScheduler();
    for (;;) { }
}
int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--configuration-json") == 0) {
        const motion_config_t m = motion_default_config();
        const plant_config_t p = plant_default_config();
        watchdog_host_state_t w;
        if (!platform_init() || !watchdog_host_get_state(&w)) { return EXIT_FAILURE; }
        printf("{\"kind\":\"compiled_configuration\",\"motion_hz\":%u,\"safety_hz\":%u,\"telemetry_hz\":%u,"
               "\"command_queue_depth\":%u,\"plant_hz\":%u,\"kp\":%.6g,\"ki\":%.6g,\"kd\":%.6g,"
               "\"pwm_min\":%.6g,\"pwm_max\":%.6g,\"tolerance_counts\":%.6g,\"completion_velocity_counts_s\":%.6g,"
               "\"dwell_cycles\":%u,\"no_motion_timeout_ms\":%u,\"heartbeat_fresh_ms\":%u,"
               "\"watchdog_timeout_ms\":%" PRIu32 ",\"homing_pwm\":%.6g,\"homing_timeout_ms\":%u}\n",
               1000U / MOTION_PERIOD_MS, 1000U / SAFETY_PERIOD_MS, 1000U / TELEMETRY_PERIOD_MS,
               COMMAND_QUEUE_CAPACITY, 1000U / p.step_ms, (double)m.pid.kp, (double)m.pid.ki, (double)m.pid.kd,
               (double)m.pid.output_min, (double)m.pid.output_max, (double)m.tolerance,
               (double)m.completion_velocity, m.dwell_cycles, SAFETY_STALL_TIMEOUT_MS, HEALTH_MAXIMUM_AGE_MS,
               w.timeout_ms, (double)HOMING_PWM, HOMING_TIMEOUT_MS);
        return fflush(stdout) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--quiet") == 0) { quiet = true; }
        else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) { scenario = argv[++i]; }
        else { return EXIT_FAILURE; }
    }
    if (strcmp(scenario, "sequence") != 0 && strcmp(scenario, "positive") != 0 &&
        strcmp(scenario, "negative") != 0 && strcmp(scenario, "relative") != 0 &&
        strcmp(scenario, "zero") != 0 && strcmp(scenario, "policy") != 0 && strcmp(scenario, "stop") != 0 &&
        strcmp(scenario, "estop") != 0 && strcmp(scenario, "estop-idle") != 0 && strcmp(scenario, "stall") != 0 &&
        strcmp(scenario, "encoder-failure") != 0 && strcmp(scenario, "limit-fault") != 0 && strcmp(scenario, "negative-limit") != 0 &&
        strcmp(scenario, "watchdog") != 0 && strcmp(scenario, "watchdog-safety") != 0 && strcmp(scenario, "watchdog-comms") != 0 &&
        strcmp(scenario, "estop-telemetry") != 0 && !known_homing_scenario()) { return EXIT_FAILURE; }
    plant_config_t config = plant_default_config();
    double initial_position = PLANT_DEFAULT_INITIAL_POSITION;
    if (strncmp(scenario, "homing", 6U) == 0) { initial_position = 1500.0; }
    if (strcmp(scenario, "homing-500") == 0) { initial_position = 500.0; }
    if (strcmp(scenario, "homing-4000") == 0) { initial_position = 4000.0; }
    if (strcmp(scenario, "homing-already") == 0) { initial_position = 10.0; }
    if (strcmp(scenario, "homing-timeout") == 0) { config.min_position = -10000.0; }
    if (strcmp(scenario, "homing-reference") == 0) { config.min_position = 100.0; config.max_position = 5100.0; initial_position = 1600.0; }
    if (MOTION_PERIOD_MS % config.step_ms != 0U || !platform_init() ||
        !plant_init(&plant, &config, initial_position) || !runtime_init()) { return EXIT_FAILURE; }
    if (strcmp(scenario, "homing-reference") == 0 && !encoder_set_reference(7777)) { return EXIT_FAILURE; }
    runtime_set_stepped_motion(true);
    runtime_set_heartbeat_filter(heartbeat_filter);
    if (!host_access_configure(lock, unlock) || !host_input_init(NULL, 0U, runtime_input_ready_from_isr)) { return EXIT_FAILURE; }
    host_set_log_sink(diagnostics_text);
    host_telemetry_quiet(true); /* Playback after scheduling avoids competing console writers. */
    if (strcmp(scenario, "estop-telemetry") == 0) {
        if (!uart_host_set_tx_sink(failed_sink)) { return EXIT_FAILURE; }
        host_telemetry_quiet(false);
    }
    if (xTaskCreateStatic(harness, "ClosedLoop", configMINIMAL_STACK_SIZE * 2U, NULL,
                         TELEMETRY_PRIORITY, harness_stack, &harness_control) == NULL) { return EXIT_FAILURE; }
    vTaskStartScheduler();
    (void)host_access_configure(NULL, NULL);
    if (strcmp(scenario, "estop-telemetry") == 0) {
        uart_host_stats_t stats;
        passed = passed && uart_host_get_stats(&stats) && stats.tx_errors > 0U;
    }
    passed = passed && parked && snapshot.tasks_started == RUNTIME_ALL_TASKS && snapshot.control_errors == 0U &&
        snapshot.queued == snapshot.consumed && snapshot.rejected == 0U && motor_get_commanded_output() == 0.0f;
    for (size_t i = 0U; i < record_count; ++i) {
        const move_record_t *r = &records[i];
        if (!quiet) {
            printf("MOVE start=%" PRId32 " target=%" PRId32 " ACK ACCEPTED; STATE IDLE -> MOVING\n", r->metrics.start, r->metrics.target);
            for (size_t j = 0U; j < r->trace_count; ++j) {
                const motion_snapshot_t *s = &r->trace[j];
                printf("CONTROL t_ms=%" PRIu32 " state=%s target=%" PRId32 " position=%" PRId32
                       " velocity=%.1f error=%.1f pwm=%.4f dwell=%" PRIu32 "\n",
                       r->trace_ms[j], application_state_name(s->state), s->target, s->position,
                       (double)s->velocity, (double)s->error, (double)s->pwm, s->stable_cycles);
            }
            if (r->metrics.completed) { puts("MOTION COMPLETE; STATE MOVING -> IDLE; PWM=0"); }
        }
        printf("METRIC start=%" PRId32 " target=%" PRId32 " final=%" PRId32 " error=%.1f overshoot=%.1f"
               " settling_ms=%" PRIu32 " completion_ms=%" PRIu32 " max_pwm=%.4f observed_ms=%" PRIu32 " hash=%" PRIx64 "\n",
               r->metrics.start, r->metrics.target, r->metrics.final_position, (double)r->metrics.final_error,
               (double)r->metrics.overshoot, r->metrics.settling_ms, r->metrics.completion_ms,
               (double)r->metrics.maximum_pwm, r->metrics.elapsed_ms, r->metrics.output_hash);
    }
    for (size_t i = 0U; i < home_count; ++i) {
        const home_record_t *r = &home_records[i];
        printf("HOME_METRIC start_logical=%" PRId32 " start_raw=%" PRId32 " seek_pwm=%.2f timeout_ms=%u"
               " elapsed_ms=%" PRIu32 " raw_end=%" PRId32 " logical_end=%" PRId32 " physical_end=%.3f"
               " reference_offset=%" PRId64 " result=%s state=%s applied_pwm=%.2f\n",
               r->start_logical, r->start_raw, (double)HOMING_PWM, HOMING_TIMEOUT_MS, r->elapsed_ms,
               r->final_raw, r->final_logical, r->physical_end, (int64_t)r->reference_offset,
               homing_result_name(r->result), application_state_name(r->state), (double)r->pwm);
    }
    if (!quiet) {
        for (size_t i = 0U; i < safety_trace_count; ++i) {
            const runtime_snapshot_t *s = &safety_trace[i];
            printf("SAFETY_TRACE t_ms=%" PRIu32 " state=%s fault=%s fault_ms=%" PRIu32
                   " estop=%u limits=%u/%u candidate=%u aborted=%" PRIu32
                   " trip_requested=%.4f applied=%.4f home=%s logical=%" PRId32 " home_contact=%u\n", s->timestamp_ms,
                   application_state_name(s->state), fault_name(s->safety.faults.active.code),
                   s->safety.faults.active.timestamp_ms, s->gpio.estop ? 1U : 0U,
                   s->gpio.negative_limit ? 1U : 0U, s->gpio.positive_limit ? 1U : 0U,
                   s->safety.candidate ? 1U : 0U, s->safety.aborted_moves,
                   (double)s->safety.requested_at_trip, (double)s->safety.applied_pwm,
                   homing_result_name(s->motion.homing.result), s->motion.position, s->safety.home_contact ? 1U : 0U);
        }
    }
    if (injection_ms != 0U) {
        printf("SAFETY_METRIC input_ms=%" PRIu32 " detection_ms=%" PRIu32 " zero_ms=%" PRIu32
               " input_to_detection_ms=%" PRIu32 " detection_to_zero_ms=%" PRIu32 " safety_hz=50\n",
               injection_ms, detection_ms, safe_ms, detection_ms - injection_ms, safe_ms - detection_ms);
    }
    printf("%s scenario=%s moves=%zu control_hz=100 plant_hz=1000 failure_line=%u\n",
           passed ? "PHASE7_OK" : "PHASE7_FAILED", scenario, record_count, failure_line);
    return passed && fflush(stdout) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
