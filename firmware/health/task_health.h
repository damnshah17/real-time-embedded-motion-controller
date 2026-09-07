#ifndef MOTION_TASK_HEALTH_H
#define MOTION_TASK_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { HEALTH_MOTION, HEALTH_SAFETY, HEALTH_COMMS, HEALTH_COUNT } task_health_id_t;
typedef struct {
    uint32_t count;
    uint32_t last_ms;
    bool seen;
} task_health_t;

/* Pure logic; the caller owns synchronization for shared records. */
void task_health_beat(task_health_t *health, uint32_t now_ms);
bool task_health_fresh(const task_health_t *health, uint32_t now_ms, uint32_t maximum_age_ms);

#endif
