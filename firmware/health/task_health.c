#include "health/task_health.h"

void task_health_beat(task_health_t *health, uint32_t now_ms)
{
    ++health->count;
    health->last_ms = now_ms;
    health->seen = true;
}

bool task_health_fresh(const task_health_t *health, uint32_t now_ms, uint32_t maximum_age_ms)
{
    return health->seen && (uint32_t)(now_ms - health->last_ms) <= maximum_age_ms;
}
