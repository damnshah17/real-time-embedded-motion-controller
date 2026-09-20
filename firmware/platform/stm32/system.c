#include "platform/stm32/board.h"
#include "FreeRTOS.h"
#include "task.h"

uint32_t SystemCoreClock = BOARD_CLOCK_HZ;
void SystemInit(void)
{
    /* Reset entry only: internal HSI, no PLL or external crystal dependency. */
    RCC->CR |= RCC_CR_HSION;
    uint32_t timeout = 1000000U;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0U && --timeout != 0U) { }
    if (timeout == 0U) { board_fatal(); }
    RCC->CFGR = 0U;
    timeout = 1000000U;
    while ((RCC->CFGR & RCC_CFGR_SWS) != 0U && --timeout != 0U) { }
    if (timeout == 0U) { board_fatal(); }
    RCC->CR &= ~RCC_CR_PLLON;
    SCB->CPACR |= (15UL << 20U);
    SCB->VTOR = FLASH_BASE;
    NVIC_SetPriorityGrouping(3U); /* Four implemented bits, all preemption. */
    __DSB(); __ISB();
}
void SystemCoreClockUpdate(void) { SystemCoreClock = BOARD_CLOCK_HZ; }
_Noreturn void board_fatal(void)
{
    __disable_irq();
    /* Register-only path: safe even before drivers/static state initialize. */
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    (void)RCC->APB1ENR;
    TIM3->CCER = 0U;
    TIM3->CCR1 = 0U;
    for (;;) { __WFI(); } /* Never feed IWDG after a fatal condition. */
}
void rtos_assert_failed(const char *file, int line) { (void)file; (void)line; board_fatal(); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) { (void)task; (void)name; board_fatal(); }
void Default_Handler(void) { board_fatal(); }
void HardFault_Handler(void) { board_fatal(); }
