#include "rtos/runtime_internal.h"
#include "rtos/diagnostics.h"
#include "drivers/platform.h"
#include "control/motion_controller.h"
#include "control/homing.h"

static void transition(app_state_t from, app_state_t to)
{
    if (from != to && to == APP_ESTOP) { (void)diagnostics_text("STATE -> ESTOP; MOVE_ABORTED if active; applied_pwm=0"); }
    else if (from != to && to == APP_FAULT) { (void)diagnostics_text("STATE -> FAULT; MOVE_ABORTED if active; applied_pwm=0"); }
    else if ((from == APP_FAULT || from == APP_ESTOP) && to == APP_IDLE) { (void)diagnostics_text("SAFETY RESET -> IDLE; applied_pwm=0"); }
    if (from == APP_IDLE && to == APP_MOVING) { (void)diagnostics_text("STATE IDLE -> MOVING"); }
    else if (from == APP_IDLE && to == APP_HOMING) { (void)diagnostics_text("STATE IDLE -> HOMING pwm=-0.30"); }
    else if (from == APP_HOMING && to == APP_IDLE) { (void)diagnostics_text("HOMING COMPLETE reference=0 applied_pwm=0"); }
    else if (from == APP_HOMING && to == APP_STOPPING) { (void)diagnostics_text("HOMING STOPPED applied_pwm=0; waiting for rest"); }
    else if (from == APP_MOVING && to == APP_STOPPING) { (void)diagnostics_text("STATE MOVING -> STOPPING"); }
    else if (from == APP_MOVING && to == APP_IDLE) { (void)diagnostics_text("STATE MOVING -> IDLE"); }
    else if (from == APP_STOPPING && to == APP_IDLE) { (void)diagnostics_text("STATE STOPPING -> IDLE"); }
}

void motion_task(void *argument)
{
    (void)argument;
    application_t app;
    application_init(&app);
    motion_controller_t motion;
    const motion_config_t config = motion_default_config();
    configASSERT(motion_init(&motion, &config));
    (void)diagnostics_text("RTOS scheduler_started owner=Motion");
    TickType_t release = xTaskGetTickCount();
    uint32_t previous_ms = platform_time_ms();
    bool first = true;
    for (;;) {
        runtime_motion_begin();
        runtime_checkpoint(TASK_MOTION);
        if (app.state == APP_BOOT || app.state == APP_INITIALIZING) { application_step(&app); }
        const uint32_t now_ms = platform_time_ms();
        const uint32_t gap = first ? MOTION_PERIOD_MS : now_ms - previous_ms;
        const float dt = (float)gap / 1000.0f;
        const app_state_t before_safety = motion.data.state;
        const bool sampled = motion_sample(&motion, dt);
        taskENTER_CRITICAL();
        if (!sampled) {
            if (before_safety == APP_MOVING || before_safety == APP_STOPPING || before_safety == APP_HOMING) { ++runtime_data.safety.aborted_moves; }
            if (before_safety == APP_HOMING) { homing_abort(&motion, HOMING_FAULTED); }
            safety_internal_fault(&runtime_data.safety, now_ms); runtime_safety_enforce();
        }
        runtime_safety_sync(&motion);
        taskEXIT_CRITICAL();
        transition(before_safety, motion.data.state);
        for (unsigned int i = 0U; i < 2U; ++i) {
            motion_command_t command;
            if (!command_bus_receive(&runtime_commands, &command)) { break; }
            taskENTER_CRITICAL();
            ++runtime_data.received_by_type[command.type];
            runtime_data.last_command = command;
            taskEXIT_CRITICAL();
            if (command.type == CMD_STATUS || command.type == CMD_HELP) {
                diagnostics_command(DIAG_RECEIVED, command, PARSE_OK);
            } else {
                const app_state_t before = motion.data.state;
                taskENTER_CRITICAL();
                const motion_result_t result = app.state == APP_IDLE ? runtime_safety_command(&motion, command) : MOTION_INVALID_STATE;
                taskEXIT_CRITICAL();
                transition(before, motion.data.state);
                diagnostics_motion(command, result);
                taskENTER_CRITICAL();
                runtime_data.last_motion_result = result;
                if (result == MOTION_ACCEPTED) { ++runtime_data.motion_accepted; }
                else { ++runtime_data.motion_rejected; }
                taskEXIT_CRITICAL();
            }
        }
        const app_state_t before = motion.data.state;
        const uint32_t completed = motion.data.completed_moves;
        /* Bounded scalar PID update + publication is atomic with safety/reset.
         * No waits, queue operations, logging or plant integration in this section. */
        taskENTER_CRITICAL();
        runtime_safety_sync(&motion);
        const bool active_before_update = motion.data.state == APP_MOVING || motion.data.state == APP_STOPPING || motion.data.state == APP_HOMING;
        const bool updated = sampled && runtime_control_update(&motion, dt, now_ms);
        if (!updated) {
            if (active_before_update) { ++runtime_data.safety.aborted_moves; }
            safety_internal_fault(&runtime_data.safety, now_ms); runtime_safety_enforce(); runtime_safety_sync(&motion);
        }
        runtime_data.state = runtime_data.safety.state != APP_IDLE ? runtime_data.safety.state :
            (app.state == APP_IDLE ? motion.data.state : app.state);
        runtime_data.motion = motion.data;
        runtime_data.encoder_count = motion.data.position;
        runtime_data.encoder_valid = motion.data.feedback_valid;
        runtime_data.motor_output = motion.data.pwm;
        if (!updated) { ++runtime_data.control_errors; }
        if (!first) {
            if (runtime_data.motion_min_gap_ms == 0U || gap < runtime_data.motion_min_gap_ms) { runtime_data.motion_min_gap_ms = gap; }
            if (gap > runtime_data.motion_max_gap_ms) { runtime_data.motion_max_gap_ms = gap; }
        }
        taskEXIT_CRITICAL();
        if (!updated) { (void)diagnostics_text("CONTROL INTERNAL fault output=zero"); }
        transition(before, motion.data.state);
        if (motion.data.completed_moves != completed) { (void)diagnostics_text("MOTION COMPLETE output=zero"); }
        first = false;
        previous_ms = now_ms;
        runtime_heartbeat(HEALTH_MOTION);
        runtime_motion_end(&release);
    }
}
