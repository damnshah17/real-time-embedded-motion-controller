#include "rtos/runtime_internal.h"
#include "rtos/diagnostics.h"
#include "drivers/platform.h"
#include "drivers/motor.h"

#include <string.h>

command_bus_t runtime_commands;
runtime_snapshot_t runtime_data;
static bool stopping;
static bool stepped_motion;
static TaskHandle_t step_owner;
static bool (*heartbeat_filter)(task_health_id_t);
static StaticTask_t task_control[TASK_COUNT];
static StackType_t task_stacks[TASK_COUNT][configMINIMAL_STACK_SIZE];
static TaskHandle_t task_handles[TASK_COUNT];

_Static_assert(MOTION_PERIOD_MS * configTICK_RATE_HZ >= 1000U, "Motion period must span at least one tick");
_Static_assert(MOTION_PERIOD_MS * configTICK_RATE_HZ % 1000U == 0U, "Motion period must be integral ticks");

bool runtime_init(void)
{
    static const struct {
        TaskFunction_t entry;
        const char *name;
        UBaseType_t priority;
    } definitions[TASK_COUNT] = {
        {motion_task, "Motion", MOTION_PRIORITY}, {safety_task, "Safety", SAFETY_PRIORITY},
        {comms_task, "Comms", COMMS_PRIORITY}, {telemetry_task, "Telemetry", TELEMETRY_PRIORITY}
    };
    memset(&runtime_data, 0, sizeof(runtime_data));
    runtime_data.state = APP_BOOT;
    safety_init(&runtime_data.safety, platform_time_ms());
    motor_safety_inhibit(true);
    stopping = false;
    stepped_motion = false;
    step_owner = NULL;
    if (!command_bus_init(&runtime_commands) || !diagnostics_init()) { return false; }
    for (unsigned int i = 0U; i < TASK_COUNT; ++i) {
        task_handles[i] = xTaskCreateStatic(definitions[i].entry, definitions[i].name,
            configMINIMAL_STACK_SIZE, NULL, definitions[i].priority, task_stacks[i], &task_control[i]);
        if (task_handles[i] == NULL) { return false; }
    }
    return true;
}

void runtime_snapshot(runtime_snapshot_t *snapshot)
{
    taskENTER_CRITICAL();
    *snapshot = runtime_data;
    snapshot->timestamp_ms = platform_time_ms();
    snapshot->queued = runtime_commands.sent;
    snapshot->consumed = runtime_commands.received;
    snapshot->rejected = runtime_commands.rejected;
    snapshot->queue_used = (uint32_t)uxQueueMessagesWaiting(runtime_commands.queue);
    snapshot->queue_high_water = runtime_commands.high_water;
    snapshot->diagnostic_drops = diagnostics_dropped();
    snapshot->safety.requested_pwm = motor_get_requested_output();
    snapshot->safety.applied_pwm = motor_get_commanded_output();
    taskEXIT_CRITICAL();
}

void runtime_checkpoint(runtime_task_id_t id)
{
    taskENTER_CRITICAL();
    runtime_data.tasks_started |= 1U << id;
    const bool stop = stopping;
    if (stop) { runtime_data.tasks_parked |= 1U << id; }
    taskEXIT_CRITICAL();
    if (stop) {
        if (id == TASK_MOTION) { platform_motor_disable(); }
        vTaskSuspend(NULL);
        configASSERT(0); /* The finite harness never resumes a parked task. */
    }
}

void runtime_heartbeat(task_health_id_t id)
{
    taskENTER_CRITICAL();
    if (heartbeat_filter == NULL || heartbeat_filter(id)) { task_health_beat(&runtime_data.health[id], platform_time_ms()); }
    taskEXIT_CRITICAL();
}

void runtime_input_ready_from_isr(void)
{
    /* Called inside xTaskIncrementTick's hook. It observes the yield-pending flag
     * after the hook returns, so no portYIELD_FROM_ISR belongs in this void hook. */
    vTaskNotifyGiveFromISR(task_handles[TASK_COMMS], NULL);
}

void runtime_request_stop(void)
{
    taskENTER_CRITICAL();
    stopping = true;
    taskEXIT_CRITICAL();
    if (stepped_motion) { xTaskNotifyGive(task_handles[TASK_MOTION]); xTaskNotifyGive(task_handles[TASK_SAFETY]); }
}

void runtime_set_stepped_motion(bool enabled) { stepped_motion = enabled; }
void runtime_set_heartbeat_filter(bool (*filter)(task_health_id_t)) { heartbeat_filter = filter; }
bool runtime_step_safety(void)
{
    if (!stepped_motion || stopping) { return false; }
    const TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    if (step_owner != NULL && step_owner != caller) { return false; }
    step_owner = caller;
    xTaskNotifyGive(task_handles[TASK_SAFETY]);
    return ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000U)) == 1U;
}
void runtime_safety_begin(void)
{ if (stepped_motion) { (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY); } }
void runtime_safety_end(TickType_t *release)
{
    if (stepped_motion) { xTaskNotifyGive(step_owner); }
    else { runtime_periodic_wait(release, SAFETY_PERIOD_MS, &runtime_data.safety_overruns); }
}
void runtime_input_ready(void) { xTaskNotifyGive(task_handles[TASK_COMMS]); }
bool runtime_step_motion(void)
{
    if (!stepped_motion || stopping) { return false; }
    const TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    if (step_owner != NULL && step_owner != caller) { return false; }
    step_owner = caller;
    xTaskNotifyGive(task_handles[TASK_MOTION]);
    return ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000U)) == 1U;
}
void runtime_motion_begin(void)
{
    if (stepped_motion) { (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY); }
}
void runtime_motion_end(TickType_t *release)
{
    if (stepped_motion) { xTaskNotifyGive(step_owner); }
    else { runtime_periodic_wait(release, MOTION_PERIOD_MS, &runtime_data.motion_overruns); }
}
