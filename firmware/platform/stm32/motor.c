#include "drivers/motor.h"
#include "platform/stm32/board.h"
#include <math.h>

static bool initialized, inhibited, negative_blocked;
static float requested, output;
static void apply_output(void)
{
    /* Disable the channel before direction changes; no deferred CCR preload. */
    TIM3->CCER = 0U;
    TIM3->CCR1 = 0U;
    GPIOC->BSRR = output < 0.0f ? 1U << 16U : 1U;
    if (output != 0.0f) {
        TIM3->CCR1 = (uint32_t)(fabsf(output) * 800.0f);
        TIM3->CCER = TIM_CCER_CC1E;
    }
}
void motor_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    (void)RCC->APB1ENR;
    TIM3->CR1 = 0U; TIM3->CCER = 0U; TIM3->CCR1 = 0U;
    GPIOC->BSRR = 1U << 16U;
    GPIOC->MODER = (GPIOC->MODER & ~3UL) | 1U;
    GPIOC->OTYPER &= ~1UL;
    TIM3->PSC = 0U; TIM3->ARR = 799U;
    TIM3->CCMR1 = 6U << TIM_CCMR1_OC1M_Pos;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->CR1 = TIM_CR1_CEN;
    board_af(GPIOA, 6U, 2U);
    requested = output = 0.0f;
    inhibited = true; negative_blocked = false; initialized = true;
}
motor_result_t motor_set_output(float duty)
{
    uint32_t key = board_lock();
    motor_result_t result = MOTOR_OK;
    if (!initialized) { result = MOTOR_NOT_INITIALIZED; }
    else {
        if (!isfinite(duty)) { requested = output = 0.0f; result = MOTOR_INVALID; }
        else {
            if (duty > 1.0f) { duty = 1.0f; result = MOTOR_CLAMPED; }
            if (duty < -1.0f) { duty = -1.0f; result = MOTOR_CLAMPED; }
            requested = duty;
            const bool blocked = inhibited || (negative_blocked && duty < 0.0f);
            output = blocked ? 0.0f : duty;
            if (blocked) { result = MOTOR_INHIBITED; }
        }
        apply_output();
    }
    board_unlock(key); return result;
}
void motor_disable(void)
{
    uint32_t key = board_lock(); requested = output = 0.0f;
    if (initialized) { apply_output(); } board_unlock(key);
}
void motor_safety_inhibit(bool inhibit)
{
    uint32_t key = board_lock(); inhibited = inhibit; output = 0.0f;
    if (initialized) { apply_output(); } board_unlock(key);
}
void motor_safety_block_negative(bool block)
{
    uint32_t key = board_lock(); negative_blocked = block;
    if (block && output < 0.0f) { output = 0.0f; if (initialized) { apply_output(); } }
    board_unlock(key);
}
bool motor_is_inhibited(void) { uint32_t key = board_lock(); bool v = inhibited; board_unlock(key); return v; }
float motor_get_requested_output(void) { uint32_t key = board_lock(); float v = requested; board_unlock(key); return v; }
float motor_get_commanded_output(void) { uint32_t key = board_lock(); float v = output; board_unlock(key); return v; }
