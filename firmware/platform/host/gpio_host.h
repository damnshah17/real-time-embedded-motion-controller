#ifndef MOTION_GPIO_HOST_H
#define MOTION_GPIO_HOST_H
#include "drivers/gpio.h"

bool gpio_host_set_estop(bool active);
bool gpio_host_set_negative_limit(bool active);
bool gpio_host_set_positive_limit(bool active);

#endif
