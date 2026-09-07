#include "FreeRTOS.h"
#include "rtos/hooks.h"

#include <stdio.h>
#include <stdlib.h>

static rtos_tick_callback_t tick_callback;

void rtos_set_tick_callback(rtos_tick_callback_t callback)
{
    tick_callback = callback;
}

void vApplicationTickHook(void)
{
    if (tick_callback != NULL) {
        tick_callback();
    }
}

void rtos_assert_failed(const char *file, int line)
{
    /* Fatal host diagnostic, not a recoverable controller fault handler. */
    fprintf(stderr, "FreeRTOS assertion: %s:%d\n", file, line);
    abort();
}
