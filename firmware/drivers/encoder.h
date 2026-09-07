#ifndef MOTION_ENCODER_H
#define MOTION_ENCODER_H
#include <stdbool.h>
#include <stdint.h>

void encoder_init(void);
/* Reads/reference changes fail before initialization; failed reads leave output unchanged. */
bool encoder_get_count(int32_t *count);
/* Set the current reported count without mutating the raw peripheral counter. */
bool encoder_set_reference(int32_t count);

#endif
