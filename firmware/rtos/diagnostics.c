#include "rtos/diagnostics.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "drivers/platform.h"
#include "task.h"

static QueueHandle_t queue;
static StaticQueue_t queue_control;
static uint8_t storage[DIAGNOSTIC_CAPACITY * sizeof(diagnostic_t)];
static uint32_t dropped;

bool diagnostics_init(void)
{
    dropped = 0U;
    queue = xQueueCreateStatic(DIAGNOSTIC_CAPACITY, sizeof(diagnostic_t), storage, &queue_control);
    return queue != NULL;
}

static bool send_record(const diagnostic_t *record)
{
    const bool sent = xQueueSend(queue, record, 0U) == pdPASS;
    if (!sent) {
        taskENTER_CRITICAL();
        ++dropped;
        taskEXIT_CRITICAL();
    }
    return sent;
}

bool diagnostics_text(const char *text)
{
    diagnostic_t record = {0};
    record.kind = DIAG_TEXT;
    record.timestamp_ms = platform_time_ms();
    size_t i = 0U;
    while (i + 1U < sizeof(record.text) && text[i] != '\0') {
        record.text[i] = text[i];
        ++i;
    }
    return send_record(&record);
}

void diagnostics_command(diagnostic_kind_t kind, motion_command_t command, command_parse_result_t result)
{
    diagnostic_t record = {0};
    record.kind = kind;
    record.command = command;
    record.parse_result = result;
    record.timestamp_ms = platform_time_ms();
    (void)send_record(&record);
}

bool diagnostics_receive(diagnostic_t *record)
{
    return xQueueReceive(queue, record, 0U) == pdPASS;
}

void diagnostics_motion(motion_command_t command, motion_result_t result)
{
    diagnostic_t record = {.kind = DIAG_MOTION, .command = command, .motion_result = result,
                           .timestamp_ms = platform_time_ms()};
    (void)send_record(&record);
}

uint32_t diagnostics_dropped(void)
{
    taskENTER_CRITICAL();
    const uint32_t result = dropped;
    taskEXIT_CRITICAL();
    return result;
}
