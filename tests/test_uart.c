#include "drivers/uart.h"
#include "platform/host/uart_host.h"
#include "test_check.h"
#include <string.h>

static uint8_t transmitted[UART_TX_MAX_WRITE];
static size_t transmitted_length;
static bool fail_output;
static uart_line_t incoming;
static bool pending;
static bool sink(const uint8_t *data, size_t length)
{
    if (fail_output || length > sizeof(transmitted)) { return false; }
    memcpy(transmitted, data, length);
    transmitted_length = length;
    return true;
}
static bool receive(uart_line_t *line)
{
    if (!pending) { return false; }
    *line = incoming;
    pending = false;
    return true;
}
static bool available(void) { return pending; }

int main(void)
{
    uart_line_t line = {7U, "retain"};
    const uint8_t bytes[] = {'A', 0U, 'B', '\n'};
    CHECK(uart_try_read_line(&line) == UART_NOT_INITIALIZED && line.length == 7U);
    CHECK(uart_write(bytes, sizeof(bytes)) == UART_NOT_INITIALIZED);
    CHECK(!uart_host_set_tx_sink(sink));
    CHECK(uart_init(NULL) == UART_INVALID_ARGUMENT);
    uart_config_t config = {0U};
    CHECK(uart_init(&config) == UART_INVALID_ARGUMENT);
    config.baud_rate = 115200U;
    CHECK(uart_init(&config) == UART_OK);
    CHECK(uart_try_read_line(&line) == UART_EMPTY && line.length == 7U);
    CHECK(!uart_rx_pending());
    CHECK(!uart_host_bind_rx(receive, NULL));
    CHECK(uart_host_bind_rx(receive, available));
    CHECK(uart_try_read_line(NULL) == UART_INVALID_ARGUMENT);
    incoming = (uart_line_t){6U, "STATUS"};
    pending = true;
    CHECK(uart_rx_pending());
    CHECK(uart_try_read_line(&line) == UART_OK && line.length == 6U && memcmp(line.text, "STATUS", 6U) == 0);
    CHECK(!uart_rx_pending());
    memset(incoming.text, 'X', sizeof(incoming.text));
    incoming.length = UART_RX_LINE_CAPACITY;
    pending = true;
    CHECK(uart_try_read_line(&line) == UART_OK && line.length == UART_RX_LINE_CAPACITY);
    incoming.length = UART_RX_LINE_CAPACITY + 1U;
    pending = true;
    CHECK(uart_try_read_line(&line) == UART_TOO_LONG && line.length == UART_RX_LINE_CAPACITY);
    CHECK(!uart_rx_pending());
    incoming = (uart_line_t){4U, "HELP"}; pending = true;
    CHECK(uart_try_read_line(&line) == UART_OK && line.length == 4U);
    CHECK(!uart_host_set_tx_sink(NULL));
    CHECK(uart_host_set_tx_sink(sink));
    CHECK(uart_write(bytes, sizeof(bytes)) == UART_OK);
    CHECK(transmitted_length == sizeof(bytes) && memcmp(bytes, transmitted, sizeof(bytes)) == 0);
    uint8_t maximum[UART_TX_MAX_WRITE];
    memset(maximum, 'z', sizeof(maximum));
    CHECK(uart_write(maximum, sizeof(maximum)) == UART_OK && transmitted_length == sizeof(maximum));
    CHECK(uart_write(maximum, sizeof(maximum) + 1U) == UART_TOO_LONG);
    CHECK(uart_write(NULL, 1U) == UART_INVALID_ARGUMENT);
    CHECK(uart_write(NULL, 0U) == UART_OK);
    fail_output = true;
    CHECK(uart_write(bytes, sizeof(bytes)) == UART_IO_ERROR);
    fail_output = false;
    CHECK(uart_write(bytes, sizeof(bytes)) == UART_OK);
    uart_host_stats_t stats;
    CHECK(uart_host_get_stats(&stats));
    CHECK(stats.initialized && stats.baud_rate == 115200U && stats.tx_errors == 3U);
    CHECK(stats.tx_calls == 4U && stats.tx_bytes == UART_TX_MAX_WRITE + 2U * sizeof(bytes));
    CHECK(uart_init(NULL) == UART_INVALID_ARGUMENT);
    CHECK(uart_host_get_stats(&stats) && stats.tx_calls == 4U); /* Failed init preserves configuration. */
    return EXIT_SUCCESS;
}
