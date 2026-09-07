#include "app/application.h"
#include "drivers/platform.h"
#include "platform/host/platform_host.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    application_t app;
    if (!platform_init()) {
        platform_motor_disable();
        return EXIT_FAILURE;
    }

    application_init(&app);
    /* Finite deterministic startup demonstration, not an RTOS scheduler. */
    for (unsigned int step = 0U; step < 2U; ++step) {
        host_advance_time(1U);
        application_step(&app);
    }

    if (app.state != APP_IDLE || host_motor_enabled()) {
        return EXIT_FAILURE;
    }
    if (printf("PHASE1_OK state=%s logical_ms=%" PRIu32 " motor_enabled=%u\n",
               application_state_name(app.state), platform_time_ms(),
               host_motor_enabled() ? 1U : 0U) < 0) {
        return EXIT_FAILURE;
    }
    return fflush(stdout) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
