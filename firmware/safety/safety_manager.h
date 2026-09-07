#ifndef MOTION_SAFETY_MANAGER_H
#define MOTION_SAFETY_MANAGER_H
#include "safety/fault_manager.h"
#include "control/motion_controller.h"
#include "drivers/gpio.h"
#define SAFETY_STALL_PWM 0.3f
#define SAFETY_STALL_VELOCITY 25.0f
#define SAFETY_STALL_TIMEOUT_MS 500U
#define SAFETY_STARTUP_GRACE_MS 200U
typedef struct {
    fault_manager_t faults;
    app_state_t state;
    gpio_inputs_t inputs;
    bool inputs_valid, healthy, candidate, encoder_recovery_required, internal_active;
    uint32_t unhealthy_mask, candidate_ms, candidate_start_ms, detection_ms, zero_ms;
    uint32_t startup_ms, watchdog_refreshes, aborted_moves;
    int32_t fault_position;
    float requested_pwm, applied_pwm;
    float requested_at_trip;
    bool home_contact; /* Granted only by successful HOME; revoked on switch release or any trip. */
} safety_manager_t;
void safety_init(safety_manager_t *s, uint32_t now);
void safety_evaluate(safety_manager_t *s, uint32_t now, gpio_inputs_t inputs, bool valid,
                     const motion_snapshot_t *motion, float pwm, uint32_t unhealthy_mask);
void safety_estop(safety_manager_t *s, uint32_t now);
void safety_internal_fault(safety_manager_t *s, uint32_t now);
void safety_homing_timeout(safety_manager_t *s, uint32_t now);
bool safety_reset(safety_manager_t *s);
#endif
