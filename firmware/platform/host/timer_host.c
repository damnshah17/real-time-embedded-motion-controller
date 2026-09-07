#include "drivers/timer.h"
#include "platform/host/timer_host.h"
#include "platform/host/host_access.h"
#include <stddef.h>

static uint32_t logical_ms;
static uint32_t resolution;
static uint32_t (*source)(void);
static bool initialized;

void timer_init(void) { logical_ms = 0U; resolution = 1U; source = NULL; initialized = true; }
uint32_t timer_now_ms(void)
{
    host_access_enter();
    const uint32_t result = source != NULL ? source() : logical_ms;
    host_access_leave();
    return result;
}
uint32_t timer_resolution_ms(void) { return resolution; }
bool timer_host_set_source(uint32_t (*clock_ms)(void), uint32_t resolution_ms)
{
    if (!initialized || clock_ms == NULL || resolution_ms == 0U || logical_ms != 0U || source != NULL) { return false; }
    source = clock_ms;
    resolution = resolution_ms;
    return true;
}
bool timer_host_advance_ms(uint32_t milliseconds)
{
    host_access_enter();
    const bool allowed = initialized && source == NULL;
    if (allowed) { logical_ms += milliseconds; }
    host_access_leave();
    return allowed;
}
