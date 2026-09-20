#include "platform/stm32/board.h"
#include "drivers/platform.h"
#include "rtos/runtime.h"
#include "FreeRTOS.h"
#include "task.h"
int main(void)
{
    /* Reset keeps PRIMASK set; the official port unmasks when first task starts. */
    if (!platform_init() || !runtime_init()) { board_fatal(); }
    NVIC_ClearPendingIRQ(USART2_IRQn);
    NVIC_EnableIRQ(USART2_IRQn);
    vTaskStartScheduler();
    board_fatal();
}
