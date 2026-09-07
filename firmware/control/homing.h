#ifndef MOTION_HOMING_H
#define MOTION_HOMING_H
#include "control/motion_controller.h"
#include "drivers/gpio.h"
#define HOMING_PWM (-0.30f)
#define HOMING_TIMEOUT_MS 40000U
/* Caller owns state/gate synchronization. No RTOS or simulator dependency. */
motion_result_t homing_begin(motion_controller_t *motion, uint32_t now);
bool homing_update(motion_controller_t *motion, gpio_inputs_t inputs, uint32_t now);
void homing_abort(motion_controller_t *motion, homing_result_t result);
const char *homing_result_name(homing_result_t result);
#endif
