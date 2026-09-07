#include "rtos/runtime_internal.h"
#include "drivers/gpio.h"
#include "drivers/watchdog.h"
#include "rtos/diagnostics.h"

void safety_task(void *argument)
{
    (void)argument;
    TickType_t release = xTaskGetTickCount();
    for (;;) {
        runtime_safety_begin();
        runtime_checkpoint(TASK_SAFETY);
        runtime_heartbeat(HEALTH_SAFETY);
        taskENTER_CRITICAL();
        const app_state_t before = runtime_data.safety.state;
        runtime_safety_evaluate();
        if (runtime_data.safety.healthy && watchdog_refresh()) { ++runtime_data.safety.watchdog_refreshes; }
        const app_state_t after = runtime_data.safety.state;
        const fault_code_t fault = runtime_data.safety.faults.active.code;
        taskEXIT_CRITICAL();
        if (before != after) {
            (void)diagnostics_text(after == APP_ESTOP ? "SAFETY ESTOP_ACTIVE applied_pwm=0" : "SAFETY FAULT applied_pwm=0");
            (void)diagnostics_text(fault_name(fault));
        }
        runtime_safety_end(&release);
    }
}
