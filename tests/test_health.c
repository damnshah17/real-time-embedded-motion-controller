#include "health/task_health.h"
#include "test_check.h"

int main(void)
{
    task_health_t tasks[HEALTH_COUNT] = {0};
    CHECK(!task_health_fresh(&tasks[HEALTH_MOTION], 0U, 150U));
    task_health_beat(&tasks[HEALTH_MOTION], 100U);
    task_health_beat(&tasks[HEALTH_SAFETY], 200U);
    CHECK(task_health_fresh(&tasks[HEALTH_MOTION], 250U, 150U));
    CHECK(!task_health_fresh(&tasks[HEALTH_MOTION], 251U, 150U));
    CHECK(task_health_fresh(&tasks[HEALTH_SAFETY], 251U, 150U));
    CHECK(!task_health_fresh(&tasks[HEALTH_COMMS], 251U, 150U));
    task_health_beat(&tasks[HEALTH_MOTION], 260U);
    CHECK(tasks[HEALTH_MOTION].count == 2U);
    CHECK(task_health_fresh(&tasks[HEALTH_MOTION], 260U, 0U));
    task_health_beat(&tasks[HEALTH_COMMS], UINT32_MAX - 9U);
    CHECK(task_health_fresh(&tasks[HEALTH_COMMS], 10U, 20U));
    CHECK(!task_health_fresh(&tasks[HEALTH_COMMS], 11U, 20U));
    tasks[HEALTH_COMMS].count = UINT32_MAX;
    task_health_beat(&tasks[HEALTH_COMMS], 12U);
    CHECK(tasks[HEALTH_COMMS].count == 0U);
    CHECK(task_health_fresh(&tasks[HEALTH_COMMS], 12U, 0U));
    return EXIT_SUCCESS;
}
