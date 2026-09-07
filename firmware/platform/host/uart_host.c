#include "drivers/uart.h"
#include "platform/host/uart_host.h"
#include "platform/host/host_access.h"
#include <stdio.h>

static uart_host_stats_t stats;
static bool (*receive_frame)(uart_line_t *);
static bool (*rx_pending)(void);
static bool (*tx_sink)(const uint8_t *, size_t);

static bool console_sink(const uint8_t *data, size_t length)
{
    return fwrite(data, 1U, length, stdout) == length && fflush(stdout) == 0;
}
uart_result_t uart_init(const uart_config_t *config)
{
    if (config == NULL || config->baud_rate == 0U) { return UART_INVALID_ARGUMENT; }
    stats = (uart_host_stats_t){true, config->baud_rate, 0U, 0U, 0U};
    receive_frame = NULL;
    rx_pending = NULL;
    tx_sink = console_sink;
    return UART_OK;
}
bool uart_host_bind_rx(bool (*receive)(uart_line_t *), bool (*pending)(void))
{
    if (!stats.initialized || receive == NULL || pending == NULL) { return false; }
    receive_frame = receive;
    rx_pending = pending;
    return true;
}
bool uart_host_set_tx_sink(bool (*sink)(const uint8_t *, size_t))
{
    if (!stats.initialized || sink == NULL) { return false; }
    tx_sink = sink;
    return true;
}
uart_result_t uart_try_read_line(uart_line_t *line)
{
    if (line == NULL) { return UART_INVALID_ARGUMENT; }
    if (!stats.initialized) { return UART_NOT_INITIALIZED; }
    if (receive_frame == NULL) { return UART_EMPTY; }
    uart_line_t frame;
    if (!receive_frame(&frame)) { return UART_EMPTY; }
    if (frame.length > UART_RX_LINE_CAPACITY) { return UART_TOO_LONG; }
    *line = frame;
    return UART_OK;
}
bool uart_rx_pending(void)
{
    return stats.initialized && rx_pending != NULL && rx_pending();
}
uart_result_t uart_write(const uint8_t *data, size_t length)
{
    if (!stats.initialized) { return UART_NOT_INITIALIZED; }
    uart_result_t result = UART_OK;
    if (length > UART_TX_MAX_WRITE) { result = UART_TOO_LONG; }
    else if (data == NULL && length != 0U) { result = UART_INVALID_ARGUMENT; }
    else if (length != 0U && !tx_sink(data, length)) { result = UART_IO_ERROR; }
    /* No lock is held around the potentially blocking host output operation. */
    host_access_enter();
    if (result == UART_OK) { ++stats.tx_calls; stats.tx_bytes += (uint32_t)length; }
    else { ++stats.tx_errors; }
    host_access_leave();
    return result;
}
bool uart_host_get_stats(uart_host_stats_t *result)
{
    if (result == NULL) { return false; }
    host_access_enter();
    *result = stats;
    host_access_leave();
    return true;
}
