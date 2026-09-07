#ifndef MOTION_PLATFORM_HOST_H
#define MOTION_PLATFORM_HOST_H

#include <stdbool.h>
#include <stdint.h>

/* Simulator-only controls; application code must not include this header. */
void host_advance_time(uint32_t milliseconds);
bool host_motor_enabled(void);

/* Install once before scheduling; NULL uses bounded host UART output. */
void host_set_log_sink(bool (*log_sink)(const char *));

#endif
