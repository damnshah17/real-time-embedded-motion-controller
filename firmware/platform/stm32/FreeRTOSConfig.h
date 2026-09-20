#ifndef MOTION_FREERTOS_CONFIG_H
#define MOTION_FREERTOS_CONFIG_H

#include <stdint.h>

void rtos_assert_failed(const char *file, int line);
#define configASSERT(condition) do { if (!(condition)) rtos_assert_failed(__FILE__, __LINE__); } while (0)
#define configUSE_PREEMPTION                     1
#define configUSE_TIME_SLICING                   1
#define configCPU_CLOCK_HZ                       16000000U
#define configTICK_RATE_HZ                       100U
#define configMAX_PRIORITIES                     5U
#define configMINIMAL_STACK_SIZE                 1024U
#define configMAX_TASK_NAME_LEN                  16U
#define configTICK_TYPE_WIDTH_IN_BITS            TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                  1
#define configSUPPORT_STATIC_ALLOCATION          1
#define configSUPPORT_DYNAMIC_ALLOCATION         0
#define configKERNEL_PROVIDED_STATIC_MEMORY      1
#define configUSE_TICK_HOOK                      0
#define configUSE_IDLE_HOOK                      0
#define configUSE_TIMERS                         0
#define configUSE_MUTEXES                        0
#define configUSE_RECURSIVE_MUTEXES              0
#define configUSE_COUNTING_SEMAPHORES            0
#define configUSE_TASK_NOTIFICATIONS             1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES    1
#define configQUEUE_REGISTRY_SIZE                0
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_TRACE_FACILITY                 0
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xTaskDelayUntil                  1
#define INCLUDE_vTaskSuspend                     1
#define INCLUDE_vTaskDelete                      1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_uxTaskPriorityGet                1

#define configPRIO_BITS 4U
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15U
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5U
#define configKERNEL_INTERRUPT_PRIORITY (15U << 4U)
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (5U << 4U)
#endif
