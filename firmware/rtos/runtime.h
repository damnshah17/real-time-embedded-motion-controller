#ifndef MOTION_RUNTIME_H
#define MOTION_RUNTIME_H

#include "app/application.h"
#include "health/task_health.h"
#include "protocol/command.h"
#include "drivers/gpio.h"
#include "control/motion_controller.h"
#include "safety/safety_manager.h"
#include <stdbool.h>

#define SAFETY_PRIORITY 4U
#define MOTION_PRIORITY 3U
#define COMMS_PRIORITY 2U
#define TELEMETRY_PRIORITY 1U
#define MOTION_PERIOD_MS 10U
#define SAFETY_PERIOD_MS 20U
#define COMMS_HEALTH_PERIOD_MS 50U
#define TELEMETRY_PERIOD_MS 100U
#define HEALTH_MAXIMUM_AGE_MS 150U
#define RUNTIME_ALL_TASKS 15U

typedef struct {
    app_state_t state;
    uint32_t timestamp_ms;
    task_health_t health[HEALTH_COUNT];
    uint32_t tasks_started;
    uint32_t tasks_parked;
    uint32_t queued;
    uint32_t consumed;
    uint32_t rejected;
    uint32_t queue_used;
    uint32_t queue_high_water;
    uint32_t parse_errors;
    uint32_t input_notifications;
    uint32_t received_by_type[CMD_HELP + 1U];
    motion_command_t last_command;
    uint32_t motion_min_gap_ms;
    uint32_t motion_max_gap_ms;
    uint32_t motion_overruns;
    uint32_t safety_overruns;
    uint32_t telemetry_cycles;
    uint32_t diagnostic_drops;
    uint32_t uart_rx_errors;
    int32_t encoder_count;
    bool encoder_valid;
    float motor_output;
    gpio_inputs_t gpio;
    bool gpio_valid;
    motion_snapshot_t motion;
    motion_result_t last_motion_result;
    uint32_t motion_accepted, motion_rejected, control_errors;
    safety_manager_t safety;
} runtime_snapshot_t;

/* One initialization per process, before scheduler startup. Creates four tasks. */
bool runtime_init(void);
/* Startup-only external cycle release; default remains periodic RTOS time. */
void runtime_set_stepped_motion(bool enabled);
bool runtime_step_motion(void); /* Single task owner; notification handshake. */
bool runtime_step_safety(void);
/* Startup-only injectable heartbeat observation filter; NULL accepts all beats. */
void runtime_set_heartbeat_filter(bool (*filter)(task_health_id_t));
void runtime_input_ready(void); /* Task-context notification. */
void runtime_snapshot(runtime_snapshot_t *snapshot);
/* ISR callback after runtime_init. Adapter supplies data and requests an ISR yield. */
void runtime_input_ready_from_isr(void);
/* Host harness shutdown: tasks park voluntarily outside work/stdio sections. */
void runtime_request_stop(void);

#endif
