#ifndef MOTION_APPLICATION_H
#define MOTION_APPLICATION_H

#include <stdint.h>

typedef enum {
    APP_BOOT,
    APP_INITIALIZING,
    APP_IDLE,
    APP_MOVING,
    APP_STOPPING,
    APP_FAULT,
    APP_ESTOP,
    APP_HOMING
} app_state_t;

typedef struct {
    app_state_t state;
    uint32_t last_transition_ms;
} application_t;

/* The execution adapter exclusively owns this object (Motion in the RTOS build). */
void application_init(application_t *app);
void application_step(application_t *app);
const char *application_state_name(app_state_t state);

#endif
