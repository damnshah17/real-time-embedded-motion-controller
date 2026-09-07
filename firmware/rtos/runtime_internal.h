#ifndef MOTION_RUNTIME_INTERNAL_H
#define MOTION_RUNTIME_INTERNAL_H

#include "rtos/runtime.h"
#include "rtos/command_bus.h"
#include "task.h"

typedef enum { TASK_MOTION, TASK_SAFETY, TASK_COMMS, TASK_TELEMETRY, TASK_COUNT } runtime_task_id_t;
extern command_bus_t runtime_commands;
extern runtime_snapshot_t runtime_data;

void runtime_checkpoint(runtime_task_id_t id);
void runtime_motion_begin(void);
void runtime_motion_end(TickType_t *release);
void runtime_safety_begin(void);
void runtime_safety_end(TickType_t *release);
void runtime_safety_evaluate(void); /* Caller holds task critical section. */
void runtime_safety_enforce(void);
void runtime_safety_sync(motion_controller_t *motion);
motion_result_t runtime_safety_command(motion_controller_t *motion, motion_command_t command);
bool runtime_control_update(motion_controller_t *motion, float dt, uint32_t now);
void runtime_heartbeat(task_health_id_t id);
void runtime_periodic_wait(TickType_t *release, uint32_t period_ms, uint32_t *overruns);
void motion_task(void *argument);
void safety_task(void *argument);
void comms_task(void *argument);
void telemetry_task(void *argument);

#endif
