#ifndef MOTION_STM32_BOARD_H
#define MOTION_STM32_BOARD_H
#include "stm32f407xx.h"
#include <stdint.h>
#define BOARD_CLOCK_HZ 16000000U
#define BOARD_UART_IRQ_PRIORITY 6U
/* Board wiring profile: safety inputs share one bank for a coherent IDR read.
 * Change these definitions together for a different harness, not application code. */
#define BOARD_INPUT_PORT GPIOC
#define BOARD_INPUT_CLOCK RCC_AHB1ENR_GPIOCEN
#define BOARD_ESTOP_PIN 1U
#define BOARD_NEGATIVE_LIMIT_PIN 2U
#define BOARD_POSITIVE_LIMIT_PIN 3U
#define BOARD_INPUT_ACTIVE_HIGH 1U
#define BOARD_INPUT_PULL 2U /* 0 none, 1 pull-up, 2 pull-down. */
/* PRIMASK preserves callers' masks, including pre-scheduler startup. */
static inline uint32_t board_lock(void) { uint32_t key = __get_PRIMASK(); __disable_irq(); __DMB(); return key; }
static inline void board_unlock(uint32_t key) { __DMB(); __set_PRIMASK(key); }
static inline void board_af(GPIO_TypeDef *port, unsigned pin, unsigned af)
{
    port->AFR[pin / 8U] = (port->AFR[pin / 8U] & ~(15UL << ((pin % 8U) * 4U))) | (af << ((pin % 8U) * 4U));
    port->MODER = (port->MODER & ~(3UL << (pin * 2U))) | (2UL << (pin * 2U));
    port->OTYPER &= ~(1UL << pin);
    port->OSPEEDR |= 2UL << (pin * 2U);
}
_Noreturn void board_fatal(void);
#endif
