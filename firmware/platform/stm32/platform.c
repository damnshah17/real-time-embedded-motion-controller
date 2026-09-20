#include "drivers/platform.h"
#include "drivers/motor.h"
#include "drivers/encoder.h"
#include "drivers/gpio.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "drivers/watchdog.h"
#include "rtos/diagnostics.h"
bool platform_init(void)
{
    motor_init(); timer_init(); gpio_init(); encoder_init();
    const uart_config_t config = {115200U};
    return uart_init(&config) == UART_OK && watchdog_init(1000U);
}
void platform_motor_disable(void) { motor_disable(); }
uint32_t platform_time_ms(void) { return timer_now_ms(); }
bool platform_log(const char *message) { return diagnostics_text(message); }
