#ifndef MOTION_MOTOR_H
#define MOTION_MOTOR_H

#include <stdbool.h>
typedef enum { MOTOR_OK, MOTOR_CLAMPED, MOTOR_INVALID, MOTOR_NOT_INITIALIZED, MOTOR_INHIBITED } motor_result_t;
/* Safety authority only. Assert atomically zeros output; release never replays PWM. */
void motor_safety_inhibit(bool inhibit);
/* Narrow post-home departure guard; positive duty still obeys the full gate. */
void motor_safety_block_negative(bool block);
bool motor_is_inhibited(void);
float motor_get_requested_output(void);
/* Startup-only initialization; output becomes zero. Target may also assert inhibit. */
void motor_init(void);
/* Task context. Finite values saturate to [-1,1]; NaN/Inf disable and return INVALID. */
motor_result_t motor_set_output(float duty_cycle);
void motor_disable(void);
float motor_get_commanded_output(void);

#endif
