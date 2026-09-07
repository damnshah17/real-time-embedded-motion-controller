#ifndef MOTION_INPUT_HOST_H
#define MOTION_INPUT_HOST_H

#include "drivers/input.h"

#define HOST_INPUT_QUEUE_CAPACITY 16U
#define HOST_SCRIPT_CAPACITY 32U
typedef struct {
    uint32_t at_ms;
    input_line_t line;
} host_input_event_t;
typedef struct {
    uint32_t released;
    uint32_t accepted;
    uint32_t dropped;
    uint32_t irq_notifications;
} host_input_stats_t;

/* Immutable, time-ordered script copied into static storage before scheduling. */
bool host_input_init(const host_input_event_t *events, size_t count, void (*ready_from_isr)(void));
void host_input_tick_isr(void);
/* Task injection. Caller notifies Comms through its task API on success. */
bool host_input_submit(const input_line_t *line);
void host_input_stop(void);
void host_input_stats(host_input_stats_t *stats);

#endif
