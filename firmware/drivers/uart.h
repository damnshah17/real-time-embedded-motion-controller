#ifndef MOTION_UART_H
#define MOTION_UART_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UART_RX_LINE_CAPACITY 64U
#define UART_TX_MAX_WRITE 256U
typedef struct { size_t length; char text[UART_RX_LINE_CAPACITY]; } uart_line_t;
typedef struct { uint32_t baud_rate; } uart_config_t;
typedef enum {
    UART_OK, UART_EMPTY, UART_NOT_INITIALIZED, UART_INVALID_ARGUMENT, UART_TOO_LONG, UART_IO_ERROR
} uart_result_t;

/* Startup-only. Baud rate is configuration metadata in the host frame model. */
uart_result_t uart_init(const uart_config_t *config);
/* One complete bounded frame; no NUL required. EMPTY/error leaves output unchanged. */
uart_result_t uart_try_read_line(uart_line_t *line);
bool uart_rx_pending(void);
/* Single task TX owner. <=256 bytes per call; errors may follow a partial physical write. */
uart_result_t uart_write(const uint8_t *data, size_t length);

#endif
