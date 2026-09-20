#include "platform/stm32/board.h"
#include "drivers/gpio.h"
#include "drivers/timer.h"
#include "drivers/watchdog.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>
static bool gpio_ready, watchdog_ready;
void gpio_init(void)
{
    RCC->AHB1ENR |= BOARD_INPUT_CLOCK; (void)RCC->AHB1ENR;
    const unsigned pins[] = {BOARD_ESTOP_PIN, BOARD_NEGATIVE_LIMIT_PIN, BOARD_POSITIVE_LIMIT_PIN};
    for (unsigned i = 0U; i < 3U; ++i) {
        const unsigned shift = pins[i] * 2U;
        BOARD_INPUT_PORT->MODER &= ~(3UL << shift);
        BOARD_INPUT_PORT->PUPDR = (BOARD_INPUT_PORT->PUPDR & ~(3UL << shift)) | (BOARD_INPUT_PULL << shift);
    }
    gpio_ready = true;
}
bool gpio_read_inputs(gpio_inputs_t *inputs)
{
    if (!gpio_ready || inputs == NULL) { return false; }
    const uint32_t pins = BOARD_INPUT_ACTIVE_HIGH ? BOARD_INPUT_PORT->IDR : ~BOARD_INPUT_PORT->IDR;
    *inputs = (gpio_inputs_t){(pins & (1UL << BOARD_ESTOP_PIN)) != 0U,
                             (pins & (1UL << BOARD_NEGATIVE_LIMIT_PIN)) != 0U,
                             (pins & (1UL << BOARD_POSITIVE_LIMIT_PIN)) != 0U};
    return true;
}
void timer_init(void) { /* SysTick is configured by the FreeRTOS Cortex-M4F port. */ }
uint32_t timer_now_ms(void) { return (uint32_t)xTaskGetTickCount() * (1000U / configTICK_RATE_HZ); }
uint32_t timer_resolution_ms(void) { return 1000U / configTICK_RATE_HZ; }
bool watchdog_init(uint32_t timeout_ms)
{
    /* LSI nominal 32 kHz / 256 = 125 Hz. Hardware range 8..32768 ms. */
    if (watchdog_ready || timeout_ms < 8U || timeout_ms > 32768U) { return false; }
    RCC->CSR |= RCC_CSR_LSION;
    uint32_t wait = 1000000U;
    while ((RCC->CSR & RCC_CSR_LSIRDY) == 0U && --wait != 0U) { }
    if (wait == 0U) { return false; }
    IWDG->KR = 0x5555U;
    IWDG->PR = 6U;
    IWDG->RLR = (timeout_ms + 7U) / 8U - 1U;
    wait = 1000000U;
    while (IWDG->SR != 0U && --wait != 0U) { }
    if (wait == 0U) { return false; }
    IWDG->KR = 0xAAAAU; IWDG->KR = 0xCCCCU;
    watchdog_ready = true; return true;
}
bool watchdog_refresh(void)
{
    if (!watchdog_ready) { return false; }
    IWDG->KR = 0xAAAAU; return true;
}
