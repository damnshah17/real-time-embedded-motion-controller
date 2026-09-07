#include "rtos/runtime_internal.h"
#include "rtos/diagnostics.h"
#include "drivers/uart.h"

_Static_assert(COMMAND_LINE_CAPACITY == UART_RX_LINE_CAPACITY, "Parser/transport frame capacities must agree");

void comms_task(void *argument)
{
    (void)argument;
    for (;;) {
        runtime_checkpoint(TASK_COMMS);
        runtime_heartbeat(HEALTH_COMMS);
        const uint32_t notifications = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(COMMS_HEALTH_PERIOD_MS));
        runtime_checkpoint(TASK_COMMS);
        taskENTER_CRITICAL();
        runtime_data.input_notifications += notifications;
        taskEXIT_CRITICAL();
        for (unsigned int i = 0U; i < 4U; ++i) {
            uart_line_t line;
            const uart_result_t received = uart_try_read_line(&line);
            if (received == UART_EMPTY) { break; }
            if (received != UART_OK) {
                taskENTER_CRITICAL();
                ++runtime_data.uart_rx_errors;
                taskEXIT_CRITICAL();
                (void)diagnostics_text("ERR UART_RX");
                break;
            }
            motion_command_t command = {CMD_STATUS, 0};
            const command_parse_result_t result = command_parse(line.text, line.length, &command);
            if (result != PARSE_OK) {
                taskENTER_CRITICAL();
                ++runtime_data.parse_errors;
                taskEXIT_CRITICAL();
                diagnostics_command(DIAG_PARSE_ERROR, command, result);
            } else {
                diagnostics_command(command_bus_send(&runtime_commands, &command) ? DIAG_QUEUED : DIAG_QUEUE_FULL,
                                    command, PARSE_OK);
            }
        }
        if (uart_rx_pending()) {
            /* Notification is a wake-up hint; queue contents are the source of truth.
             * Self-signal remaining work, then yield a tick to bound burst CPU use. */
            xTaskNotifyGive(xTaskGetCurrentTaskHandle());
            vTaskDelay(1U);
        }
    }
}
