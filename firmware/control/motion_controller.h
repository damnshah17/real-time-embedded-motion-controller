#ifndef MOTION_CONTROLLER_H
#define MOTION_CONTROLLER_H
#include "control/pid.h"
#include "app/application.h"
#include "protocol/command.h"

typedef struct {
    pid_config_t pid;
    int32_t minimum, maximum;
    float tolerance, completion_velocity;
    uint32_t dwell_cycles;
} motion_config_t;
typedef enum { MOTION_ACCEPTED, MOTION_INVALID_STATE, MOTION_OUT_OF_RANGE,
               MOTION_NOT_IMPLEMENTED, MOTION_INPUT_ERROR } motion_result_t;
typedef enum { HOMING_NONE, HOMING_RUNNING, HOMING_COMPLETE, HOMING_STOPPED,
               HOMING_ESTOPPED, HOMING_FAULTED, HOMING_TIMEOUT } homing_result_t;
typedef struct {
    homing_result_t result;
    uint32_t started_ms, elapsed_ms, completed;
    bool referenced;
} homing_snapshot_t;
typedef struct {
    app_state_t state;
    int32_t position, target;
    float velocity, error, pid_output, pwm;
    uint32_t stable_cycles, completed_moves;
    bool feedback_valid;
    homing_snapshot_t homing;
} motion_snapshot_t;
typedef struct {
    motion_config_t config;
    pid_controller_t pid;
    motion_snapshot_t data;
    int32_t previous_count;
    bool initialized, sampled;
} motion_controller_t;

motion_config_t motion_default_config(void);
bool motion_init(motion_controller_t *motion, const motion_config_t *config);
/* Single owner. Sample once, process bounded commands, then update once per cycle.
 * Feedback comes only through encoder.h; output goes only through motor.h. */
bool motion_sample(motion_controller_t *motion, float dt);
motion_result_t motion_command(motion_controller_t *motion, motion_command_t command);
bool motion_update(motion_controller_t *motion, float dt);
const char *motion_result_name(motion_result_t result);
#endif
