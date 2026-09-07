#ifndef MOTION_UART_HOST_H
#define MOTION_UART_HOST_H
#include "drivers/uart.h"

typedef struct {
    bool initialized;
    uint32_t baud_rate;
    uint32_t tx_calls;
    uint32_t tx_bytes;
    uint32_t tx_errors;
} uart_host_stats_t;

/* Bind startup-only adapters; never change them concurrently with driver calls. */
bool uart_host_bind_rx(bool (*receive)(uart_line_t *), bool (*pending)(void));
bool uart_host_set_tx_sink(bool (*sink)(const uint8_t *, size_t));
bool uart_host_get_stats(uart_host_stats_t *stats);

#endif
