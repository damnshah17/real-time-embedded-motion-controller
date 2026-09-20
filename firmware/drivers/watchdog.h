#ifndef MOTION_WATCHDOG_H
#define MOTION_WATCHDOG_H
#include <stdbool.h>
#include <stdint.h>

/* Startup configuration only: timeout must be 1..INT32_MAX milliseconds.
 * A platform may reject values outside its hardware range; STM32 uses 8..32768 ms. */
bool watchdog_init(uint32_t timeout_ms);
/* Safety supervisor only, after checking every critical heartbeat. */
bool watchdog_refresh(void);

#endif
