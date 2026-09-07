#include "drivers/gpio.h"
#include "platform/host/gpio_host.h"
#include "platform/host/host_access.h"
#include <stddef.h>

static bool initialized;
static gpio_inputs_t state;

void gpio_init(void) { state = (gpio_inputs_t){false, false, false}; initialized = true; }
bool gpio_read_inputs(gpio_inputs_t *inputs)
{
    if (inputs == NULL) { return false; }
    host_access_enter();
    const bool ready = initialized;
    if (ready) { *inputs = state; }
    host_access_leave();
    return ready;
}
static bool set_signal(bool *signal, bool active)
{
    host_access_enter();
    const bool ready = initialized;
    if (ready) { *signal = active; }
    host_access_leave();
    return ready;
}
bool gpio_host_set_estop(bool active) { return set_signal(&state.estop, active); }
bool gpio_host_set_negative_limit(bool active) { return set_signal(&state.negative_limit, active); }
bool gpio_host_set_positive_limit(bool active) { return set_signal(&state.positive_limit, active); }
