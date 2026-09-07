#include "drivers/platform.h"
#include "platform/host/platform_host.h"
#include "drivers/motor.h"
#include "drivers/encoder.h"
#include "drivers/gpio.h"
#include "drivers/timer.h"
#include "drivers/watchdog.h"
#include "drivers/uart.h"
#include "platform/host/timer_host.h"
#include "platform/host/host_access.h"

#include <inttypes.h>
#include <stdio.h>

static bool (*log_service)(const char *);

bool platform_init(void)
{
    /* Startup only. Inhibit output before configuring the other peripherals. */
    motor_init();
    timer_init();
    gpio_init();
    encoder_init();
    log_service = NULL;
    const uart_config_t uart = {115200U};
    return uart_init(&uart) == UART_OK && watchdog_init(1000U);
}

void platform_motor_disable(void)
{
    motor_disable();
}

uint32_t platform_time_ms(void)
{
    return timer_now_ms();
}

bool platform_log(const char *message)
{
    if (log_service != NULL) { return log_service(message); }
    char line[UART_TX_MAX_WRITE + 1U];
    const int length = snprintf(line, sizeof(line), "[%06" PRIu32 "] %s\n", platform_time_ms(), message);
    return length >= 0 && (size_t)length < sizeof(line) && uart_write((const uint8_t *)line, (size_t)length) == UART_OK;
}

void host_advance_time(uint32_t milliseconds)
{
    (void)timer_host_advance_ms(milliseconds);
}

bool host_motor_enabled(void)
{
    return motor_get_commanded_output() != 0.0f;
}

void host_set_log_sink(bool (*log_sink)(const char *))
{
    log_service = log_sink;
}
