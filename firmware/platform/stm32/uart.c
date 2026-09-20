#include "drivers/uart.h"
#include "platform/stm32/board.h"
#include "rtos/runtime.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <string.h>

#define RX_FRAMES 16U
static StaticQueue_t rx_control;
static uint8_t rx_storage[RX_FRAMES * sizeof(uart_line_t)];
static QueueHandle_t rx_queue;
static uart_line_t partial;
static bool skip_lf, discard;
/* Inspectable counters; no formatting, parsing, or control from the ISR. */
volatile uint32_t stm32_uart_rx_drops, stm32_uart_rx_errors, stm32_uart_tx_errors;
_Static_assert(configPRIO_BITS == __NVIC_PRIO_BITS, "NVIC priority width mismatch");
_Static_assert(BOARD_UART_IRQ_PRIORITY >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, "UART IRQ cannot call FreeRTOS");
uart_result_t uart_init(const uart_config_t *config)
{
    if (config == NULL || config->baud_rate < 1200U || config->baud_rate > 1000000U) { return UART_INVALID_ARGUMENT; }
    if (rx_queue != NULL) { return UART_INVALID_ARGUMENT; }
    rx_queue = xQueueCreateStatic(RX_FRAMES, sizeof(uart_line_t), rx_storage, &rx_control);
    if (rx_queue == NULL) { return UART_IO_ERROR; }
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN; (void)RCC->APB1ENR;
    board_af(GPIOA, 2U, 7U); board_af(GPIOA, 3U, 7U);
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(3UL << 6U)) | (1UL << 6U);
    USART2->CR1 = 0U; USART2->CR2 = 0U; USART2->CR3 = 0U;
    USART2->BRR = (BOARD_CLOCK_HZ + config->baud_rate / 2U) / config->baud_rate;
    (void)USART2->SR; (void)USART2->DR;
    NVIC_SetPriority(USART2_IRQn, BOARD_UART_IRQ_PRIORITY);
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    return UART_OK;
}
void USART2_IRQHandler(void)
{
    const uint32_t status = USART2->SR;
    if ((status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) == 0U) { return; }
    const uint8_t byte = (uint8_t)USART2->DR; /* SR then DR clears error flags. */
    if ((status & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) != 0U) {
        ++stm32_uart_rx_errors; partial.length = 0U; discard = true; return;
    }
    if (byte == '\n' && skip_lf) { skip_lf = false; return; }
    skip_lf = byte == '\r';
    if (byte == '\r' || byte == '\n') {
        if (!discard) {
            if (xQueueSendFromISR(rx_queue, &partial, NULL) == pdPASS) {
                runtime_input_ready_from_isr();
                /* Runtime notification API does not expose woken; always pend a switch. */
                portYIELD_FROM_ISR(pdTRUE);
            } else { ++stm32_uart_rx_drops; }
        }
        partial.length = 0U; discard = false;
    } else if (!discard && partial.length < UART_RX_LINE_CAPACITY) {
        partial.text[partial.length++] = (char)byte;
    }
    /* A saturated length (64) is retained and rejected by the existing parser. */
}
uart_result_t uart_try_read_line(uart_line_t *line)
{
    if (line == NULL) { return UART_INVALID_ARGUMENT; }
    if (rx_queue == NULL) { return UART_NOT_INITIALIZED; }
    return xQueueReceive(rx_queue, line, 0U) == pdPASS ? UART_OK : UART_EMPTY;
}
bool uart_rx_pending(void) { return rx_queue != NULL && uxQueueMessagesWaiting(rx_queue) != 0U; }
static bool wait_flag(uint32_t flag)
{
    uint32_t limit = 100000U;
    while ((USART2->SR & flag) == 0U && --limit != 0U) { }
    return limit != 0U;
}
uart_result_t uart_write(const uint8_t *data, size_t length)
{
    if (rx_queue == NULL) { return UART_NOT_INITIALIZED; }
    if (data == NULL && length != 0U) { return UART_INVALID_ARGUMENT; }
    if (length > UART_TX_MAX_WRITE) { return UART_TOO_LONG; }
    for (size_t i = 0U; i < length; ++i) {
        if (!wait_flag(USART_SR_TXE)) { ++stm32_uart_tx_errors; return UART_IO_ERROR; }
        USART2->DR = data[i];
    }
    if (length != 0U && !wait_flag(USART_SR_TC)) { ++stm32_uart_tx_errors; return UART_IO_ERROR; }
    return UART_OK;
}
