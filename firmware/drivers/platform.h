#ifndef MOTION_PLATFORM_H
#define MOTION_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

/* Called before application startup. Failure prevents application execution. */
bool platform_init(void);
/* Compatibility startup inhibit; delegates to the motor driver. */
void platform_motor_disable(void);
/* Compatibility clock; delegates to timer_now_ms(), modulo UINT32_MAX + 1. */
uint32_t platform_time_ms(void);
/* Best-effort diagnostic output. Failure must not change application state. */
bool platform_log(const char *message);

#endif
