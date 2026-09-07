#ifndef MOTION_FAULT_MANAGER_H
#define MOTION_FAULT_MANAGER_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { FAULT_NONE, FAULT_POSITIVE_LIMIT, FAULT_NEGATIVE_LIMIT,
    FAULT_NO_MOTION_UNDER_COMMAND, FAULT_WATCHDOG, FAULT_INTERNAL, FAULT_HOMING_TIMEOUT } fault_code_t;
typedef struct { fault_code_t code; uint32_t timestamp_ms; uint32_t source; bool condition_active; } fault_record_t;
typedef struct { fault_record_t active, history; } fault_manager_t;
void fault_raise(fault_manager_t *manager, fault_code_t code, uint32_t now, uint32_t source);
bool fault_reset(fault_manager_t *manager);
const char *fault_name(fault_code_t code);
#endif
