#include "app/application.h"
#include "drivers/platform.h"

#include <stdio.h>
#include <stdlib.h>

static uint32_t now_ms;
static unsigned int disable_calls;
static unsigned int log_calls;

/* Independent platform double: proves application links without host code. */
bool platform_init(void) { return true; }
void platform_motor_disable(void) { ++disable_calls; }
uint32_t platform_time_ms(void) { return now_ms; }
bool platform_log(const char *message)
{
    (void)message;
    ++log_calls;
    return false; /* Startup must work with a failed diagnostic sink. */
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while (0)

int main(void)
{
    application_t app;
    now_ms = 17U;
    application_init(&app);
    CHECK(app.state == APP_BOOT);
    CHECK(app.last_transition_ms == 17U);
    CHECK(disable_calls == 1U);

    now_ms = 18U;
    application_step(&app);
    CHECK(app.state == APP_INITIALIZING);
    CHECK(app.last_transition_ms == 18U);

    now_ms = 19U;
    application_step(&app);
    CHECK(app.state == APP_IDLE);
    CHECK(app.last_transition_ms == 19U);

    now_ms = 20U;
    application_step(&app);
    CHECK(app.state == APP_IDLE);
    CHECK(app.last_transition_ms == 19U);
    CHECK(disable_calls == 4U);
    CHECK(log_calls == 3U);
    return EXIT_SUCCESS;
}
