#ifndef MOTION_TELEMETRY_H
#define MOTION_TELEMETRY_H

#include "rtos/runtime.h"
#include "rtos/diagnostics.h"

/* Telemetry is the sole scheduler-time output owner. Never call from ISR. */
void platform_diagnostic_emit(const diagnostic_t *record);
void platform_telemetry_emit(const runtime_snapshot_t *snapshot);

#endif
