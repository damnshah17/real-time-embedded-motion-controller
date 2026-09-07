#include "platform/host/input_host.h"
#include "platform/host/uart_host.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include <string.h>

static StaticQueue_t rx_control;
static uint8_t rx_storage[HOST_INPUT_QUEUE_CAPACITY * sizeof(input_line_t)];
static QueueHandle_t rx_queue;
static host_input_event_t script[HOST_SCRIPT_CAPACITY];
static size_t event_count;
static size_t next_event;
static void (*input_ready)(void);
static bool active;
static host_input_stats_t counters;
static bool receive_frame(uart_line_t *line);
static bool frames_pending(void);

bool host_input_init(const host_input_event_t *events, size_t count, void (*ready_from_isr)(void))
{
    if (count > HOST_SCRIPT_CAPACITY || ready_from_isr == NULL || (count != 0U && events == NULL)) { return false; }
    for (size_t i = 0U; i < count; ++i) {
        if (events[i].line.length > UART_RX_LINE_CAPACITY ||
            events[i].at_ms % portTICK_PERIOD_MS != 0U ||
            (i > 0U && events[i].at_ms < events[i - 1U].at_ms)) { return false; }
    }
    if (count != 0U) { memcpy(script, events, count * sizeof(*events)); }
    event_count = count;
    next_event = 0U;
    memset(&counters, 0, sizeof(counters));
    input_ready = ready_from_isr;
    rx_queue = xQueueCreateStatic(HOST_INPUT_QUEUE_CAPACITY, sizeof(input_line_t), rx_storage, &rx_control);
    active = rx_queue != NULL && uart_host_bind_rx(receive_frame, frames_pending);
    return active;
}

void host_input_tick_isr(void)
{
    if (!active) { return; }
    const uint32_t now = (uint32_t)xTaskGetTickCountFromISR() * (uint32_t)portTICK_PERIOD_MS;
    bool delivered = false;
    /* Bounded host frame-release model, not a UART byte ISR or electrical timing model. */
    for (unsigned int budget = 0U; budget < HOST_INPUT_QUEUE_CAPACITY; ++budget) {
        if (next_event == event_count || script[next_event].at_ms > now) { break; }
        ++counters.released;
        if (xQueueSendFromISR(rx_queue, &script[next_event].line, NULL) == pdPASS) {
            ++counters.accepted;
            delivered = true;
        } else { ++counters.dropped; }
        ++next_event;
    }
    if (delivered) {
        ++counters.irq_notifications;
        input_ready();
    }
    /* The enclosing kernel tick performs any pending context switch. */
}

static bool receive_frame(uart_line_t *line)
{
    return xQueueReceive(rx_queue, line, 0U) == pdPASS;
}

bool host_input_submit(const input_line_t *line)
{
    if (line == NULL || line->length > UART_RX_LINE_CAPACITY) { return false; }
    taskENTER_CRITICAL();
    bool result = false;
    if (active) {
        ++counters.released;
        result = xQueueSend(rx_queue, line, 0U) == pdPASS;
        if (result) { ++counters.accepted; } else { ++counters.dropped; }
    }
    taskEXIT_CRITICAL();
    return result;
}

static bool frames_pending(void)
{
    return uxQueueMessagesWaiting(rx_queue) != 0U;
}


void host_input_stop(void)
{
    taskENTER_CRITICAL();
    active = false;
    taskEXIT_CRITICAL();
}

void host_input_stats(host_input_stats_t *stats)
{
    taskENTER_CRITICAL();
    *stats = counters;
    taskEXIT_CRITICAL();
}
