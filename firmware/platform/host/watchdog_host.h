#ifndef MOTION_WATCHDOG_HOST_H
#define MOTION_WATCHDOG_HOST_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool initialized;
    uint32_t timeout_ms;
    uint32_t refresh_count;
    uint32_t last_refresh_ms;
} watchdog_host_state_t;
bool watchdog_host_get_state(watchdog_host_state_t *state);

#endif
