#ifndef MOTION_RTOS_HOOKS_H
#define MOTION_RTOS_HOOKS_H

/* Configure once, before scheduler startup. Callback runs in tick ISR context. */
typedef void (*rtos_tick_callback_t)(void);
void rtos_set_tick_callback(rtos_tick_callback_t callback);

#endif
