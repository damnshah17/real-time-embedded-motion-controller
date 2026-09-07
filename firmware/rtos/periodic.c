#include "rtos/runtime_internal.h"

void runtime_periodic_wait(TickType_t *release, uint32_t period_ms, uint32_t *overruns)
{
    if (xTaskDelayUntil(release, pdMS_TO_TICKS(period_ms)) == pdFALSE) {
        /* Bound overdue work rather than spinning through missed releases. */
        if (overruns != NULL) {
            taskENTER_CRITICAL();
            ++*overruns;
            taskEXIT_CRITICAL();
        }
        vTaskDelay(1U);
        /* Anchor after yielding so a one-tick task can resume its normal phase. */
        *release = xTaskGetTickCount();
    }
}
