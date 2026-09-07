#include "app/application.h"
#include "drivers/platform.h"

void application_init(application_t *app)
{
    platform_motor_disable();
    app->state = APP_BOOT;
    app->last_transition_ms = platform_time_ms();
    (void)platform_log("SYSTEM state=BOOT");
}

void application_step(application_t *app)
{
    /* Startup cannot enable an actuator, including when logging fails. */
    platform_motor_disable();
    switch (app->state) {
    case APP_BOOT:
        app->state = APP_INITIALIZING;
        app->last_transition_ms = platform_time_ms();
        (void)platform_log("STATE BOOT -> INITIALIZING");
        break;
    case APP_INITIALIZING:
        app->state = APP_IDLE;
        app->last_transition_ms = platform_time_ms();
        (void)platform_log("STATE INITIALIZING -> IDLE");
        break;
    case APP_IDLE:
        break;
    default:
        /* Inhibit is already asserted; do not invent a recovery transition. */
        break;
    }
}

const char *application_state_name(app_state_t state)
{
    switch (state) {
    case APP_BOOT: return "BOOT";
    case APP_INITIALIZING: return "INITIALIZING";
    case APP_IDLE: return "IDLE";
    case APP_MOVING: return "MOVING";
    case APP_STOPPING: return "STOPPING";
    case APP_FAULT: return "FAULT";
    case APP_ESTOP: return "ESTOP";
    case APP_HOMING: return "HOMING";
    default: return "UNKNOWN";
    }
}
