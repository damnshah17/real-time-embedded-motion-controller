#include "drivers/encoder.h"
#include "platform/stm32/board.h"
#include <stddef.h>
static bool initialized;
static uint32_t reference_offset;
void encoder_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR;
    /* PA15/PB3 AF1: TIM2 CH1/CH2. Frees PA0 Discovery button; SWD remains usable. */
    board_af(GPIOA, 15U, 1U); board_af(GPIOB, 3U, 1U);
    GPIOA->PUPDR &= ~(3UL << 30U); GPIOB->PUPDR &= ~(3UL << 6U);
    TIM2->CR1 = 0U; TIM2->CCER = 0U; TIM2->PSC = 0U;
    TIM2->ARR = UINT32_MAX;
    TIM2->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;
    TIM2->SMCR = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1; /* Quadrature, both edges. */
    TIM2->EGR = TIM_EGR_UG; TIM2->CNT = 0U;
    TIM2->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;
    TIM2->CR1 = TIM_CR1_CEN;
    reference_offset = 0U; initialized = true;
}
bool encoder_get_count(int32_t *count)
{
    if (count == NULL) { return false; }
    uint32_t key = board_lock(); const bool ready = initialized;
    if (ready) {
        const uint32_t bits = TIM2->CNT + reference_offset;
        *count = bits <= INT32_MAX ? (int32_t)bits : INT32_MIN + (int32_t)(bits - UINT32_C(2147483648));
    }
    board_unlock(key); return ready;
}
bool encoder_set_reference(int32_t count)
{
    uint32_t key = board_lock(); const bool ready = initialized;
    if (ready) { reference_offset = (uint32_t)count - TIM2->CNT; }
    board_unlock(key); return ready;
}
