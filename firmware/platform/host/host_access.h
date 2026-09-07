#ifndef MOTION_HOST_ACCESS_H
#define MOTION_HOST_ACCESS_H
#include <stdbool.h>

/* Configure before scheduling. NULL/NULL means a single-threaded host run.
 * RTOS runners install critical-section hooks. Never call drivers from a native
 * concurrent producer or an ISR; the RX queue retains its separate ISR-safe path. */
bool host_access_configure(void (*enter)(void), void (*leave)(void));
void host_access_enter(void);
void host_access_leave(void);

#endif
