#include "rtos/runtime_internal.h"
#include "rtos/diagnostics.h"
#include "drivers/telemetry.h"

void telemetry_task(void *argument)
{
    (void)argument;
    TickType_t release = xTaskGetTickCount();
    for (;;) {
        runtime_checkpoint(TASK_TELEMETRY);
        diagnostic_t record;
        for (unsigned int i = 0U; i < DIAGNOSTIC_CAPACITY; ++i) {
            if (!diagnostics_receive(&record)) { break; }
            platform_diagnostic_emit(&record);
        }
        taskENTER_CRITICAL();
        ++runtime_data.telemetry_cycles;
        taskEXIT_CRITICAL();
        runtime_snapshot_t snapshot;
        runtime_snapshot(&snapshot);
        platform_telemetry_emit(&snapshot);
        runtime_periodic_wait(&release, TELEMETRY_PERIOD_MS, NULL);
    }
}
