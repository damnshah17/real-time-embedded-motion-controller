#include "FreeRTOS.h"
#include "task.h"
#include "rtos/command_bus.h"
#include "rtos/diagnostics.h"
#include "rtos/hooks.h"
#include "rtos/runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>

static StaticTask_t task_storage;
static StackType_t task_stack[configMINIMAL_STACK_SIZE];
static volatile unsigned int completed;
static volatile unsigned int failures;
static volatile bool notification_checked;
static StaticTask_t receiver_storage;
static StackType_t receiver_stack[configMINIMAL_STACK_SIZE];
static TaskHandle_t receiver;
static command_bus_t bus;

#define VERIFY(condition) do { if (!(condition)) { ++failures; } } while (0)

uint32_t platform_time_ms(void)
{
    return (uint32_t)xTaskGetTickCount() * (uint32_t)portTICK_PERIOD_MS;
}

static void tick_interrupt(void)
{
    static uint32_t ticks;
    if (++ticks == 5U) {
        vTaskNotifyGiveFromISR(receiver, NULL);
        vTaskNotifyGiveFromISR(receiver, NULL);
    }
}

static void notification_task(void *argument)
{
    (void)argument;
    VERIFY(uxTaskPriorityGet(NULL) == 2U);
    VERIFY(ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(200U)) == 2U);
    VERIFY(ulTaskNotifyTake(pdTRUE, 0U) == 1U);
    VERIFY(ulTaskNotifyTake(pdTRUE, 0U) == 0U);
    notification_checked = true;
    vTaskSuspend(NULL);
    for (;;) { }
}

static void kernel_test_task(void *argument)
{
    (void)argument;
    VERIFY(uxTaskPriorityGet(NULL) == 1U);
    motion_command_t command = {CMD_MOVE_REL, -123};
    for (unsigned int i = 0U; i < COMMAND_QUEUE_CAPACITY; ++i) {
        command.value = -(int32_t)i;
        VERIFY(command_bus_send(&bus, &command));
    }
    command.value = 999; /* Sending copied the original values. */
    VERIFY(!command_bus_send(&bus, &command));
    VERIFY(bus.high_water == COMMAND_QUEUE_CAPACITY && bus.rejected == 1U);
    for (unsigned int i = 0U; i < COMMAND_QUEUE_CAPACITY; ++i) {
        VERIFY(command_bus_receive(&bus, &command));
        VERIFY(command.type == CMD_MOVE_REL && command.value == -(int32_t)i);
    }
    VERIFY(!command_bus_receive(&bus, &command));
    command.type = CMD_STATUS;
    VERIFY(command_bus_send(&bus, &command));
    VERIFY(command_bus_receive(&bus, &command) && command.type == CMD_STATUS);
    VERIFY(bus.sent == COMMAND_QUEUE_CAPACITY + 1U && bus.sent == bus.received);

    for (unsigned int i = 0U; i < DIAGNOSTIC_CAPACITY; ++i) { VERIFY(diagnostics_text("bounded")); }
    VERIFY(!diagnostics_text("overflow"));
    VERIFY(diagnostics_dropped() == 1U);
    diagnostic_t record;
    for (unsigned int i = 0U; i < DIAGNOSTIC_CAPACITY; ++i) { VERIFY(diagnostics_receive(&record)); }
    VERIFY(!diagnostics_receive(&record));
    VERIFY(diagnostics_text("recovered"));
    VERIFY(diagnostics_receive(&record));
    uint32_t overruns = 0U;
    TickType_t overdue_release = xTaskGetTickCount();
    vTaskDelay(pdMS_TO_TICKS(30U));
    runtime_periodic_wait(&overdue_release, 10U, &overruns);
    VERIFY(overruns == 1U);
    runtime_periodic_wait(&overdue_release, 10U, &overruns);
    VERIFY(overruns == 1U); /* Recovery must not manufacture perpetual misses. */
    TickType_t release = xTaskGetTickCount();
    for (unsigned int i = 0U; i < 10U; ++i) {
        const TickType_t previous = release;
        vTaskDelayUntil(&release, pdMS_TO_TICKS(20U));
        VERIFY(release - previous == pdMS_TO_TICKS(20U));
        VERIFY(xTaskGetTickCount() >= release);
        ++completed;
    }
    vTaskEndScheduler();
    for (;;) { }
}

int main(void)
{
    if (!command_bus_init(&bus) || !diagnostics_init()) { return EXIT_FAILURE; }
    receiver = xTaskCreateStatic(notification_task, "NotifyTest", configMINIMAL_STACK_SIZE,
                                 NULL, 2U, receiver_stack, &receiver_storage);
    if (receiver == NULL) { return EXIT_FAILURE; }
    rtos_set_tick_callback(tick_interrupt);
    if (xTaskCreateStatic(kernel_test_task, "KernelTest", configMINIMAL_STACK_SIZE,
                          NULL, 1U, task_stack, &task_storage) == NULL) {
        return EXIT_FAILURE;
    }
    vTaskStartScheduler();
    if (completed != 10U || failures != 0U || !notification_checked) {
        fprintf(stderr, "kernel checks: completed=%u failures=%u notified=%u\n", completed, failures, notification_checked ? 1U : 0U);
        return EXIT_FAILURE;
    }
    puts("KERNEL_OK priorities, periodic releases, FIFO copies, queue overflow/recovery, ISR notification counting, bounded diagnostics");
    return EXIT_SUCCESS;
}
