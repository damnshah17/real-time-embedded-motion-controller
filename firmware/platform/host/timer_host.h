#ifndef MOTION_TIMER_HOST_H
#define MOTION_TIMER_HOST_H
#include <stdbool.h>
#include <stdint.h>

/* Install a monotonic source before scheduling, after timer_init. Resolution is explicit. */
bool timer_host_set_source(uint32_t (*clock_ms)(void), uint32_t resolution_ms);
/* Deterministic manual mode only; fails while an external source is selected. */
bool timer_host_advance_ms(uint32_t milliseconds);

#endif
