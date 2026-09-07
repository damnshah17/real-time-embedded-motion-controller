#include "safety/fault_manager.h"
void fault_raise(fault_manager_t *m, fault_code_t code, uint32_t now, uint32_t source)
{
    if (m->active.code == FAULT_NONE && code != FAULT_NONE) {
        m->active = (fault_record_t){code, now, source, true};
    }
}
bool fault_reset(fault_manager_t *m)
{
    if (m->active.condition_active) { return false; }
    if (m->active.code != FAULT_NONE) { m->history = m->active; }
    m->active = (fault_record_t){0};
    return true;
}
const char *fault_name(fault_code_t code)
{
    switch (code) {
    case FAULT_NONE: return "NONE";
    case FAULT_POSITIVE_LIMIT: return "POSITIVE_LIMIT";
    case FAULT_NEGATIVE_LIMIT: return "NEGATIVE_LIMIT";
    case FAULT_NO_MOTION_UNDER_COMMAND: return "NO_MOTION_UNDER_COMMAND";
    case FAULT_WATCHDOG: return "WATCHDOG";
    case FAULT_HOMING_TIMEOUT: return "HOMING_TIMEOUT";
    default: return "INTERNAL";
    }
}
