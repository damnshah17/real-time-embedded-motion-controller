#ifndef MOTION_GPIO_H
#define MOTION_GPIO_H
#include <stdbool.h>

typedef struct {
    bool estop;
    bool negative_limit;
    bool positive_limit;
} gpio_inputs_t;

void gpio_init(void);
/* Logical active states, not voltage levels. One coherent snapshot. */
bool gpio_read_inputs(gpio_inputs_t *inputs);

#endif
