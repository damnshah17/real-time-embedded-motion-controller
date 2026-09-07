#include "drivers/watchdog.h"
#include "drivers/timer.h"
#include "platform/host/watchdog_host.h"
#include "platform/host/host_access.h"
#include <stddef.h>

static watchdog_host_state_t state;

bool watchdog_init(uint32_t timeout_ms)
{
    /* Failed configuration does not replace an already valid configuration. */
    if (timeout_ms == 0U || timeout_ms > INT32_MAX || timer_resolution_ms() == 0U) { return false; }
    state = (watchdog_host_state_t){true, timeout_ms, 0U, timer_now_ms()};
    return true;
}
bool watchdog_refresh(void)
{
    host_access_enter();
    const bool ready = state.initialized;
    if (ready) { ++state.refresh_count; state.last_refresh_ms = timer_now_ms(); }
    host_access_leave();
    return ready;
}
bool watchdog_host_get_state(watchdog_host_state_t *result)
{
    if (result == NULL) { return false; }
    host_access_enter();
    *result = state;
    host_access_leave();
    return true;
}
