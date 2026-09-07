#ifndef MOTION_ENCODER_HOST_H
#define MOTION_ENCODER_HOST_H
#include <stdbool.h>
#include <stdint.h>

/* Task/single-threaded simulator context only. Raw counter, before reference offset. */
bool encoder_host_set_count(int32_t raw_count);
typedef struct { int32_t raw_count, reference_offset; } encoder_host_state_t;
/* Coherent read-only diagnostics, including frozen published counts. */
bool encoder_host_get_state(encoder_host_state_t *state);

#endif
