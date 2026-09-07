#ifndef MOTION_TIMER_H
#define MOTION_TIMER_H
#include <stdint.h>

void timer_init(void);
/* Milliseconds modulo 2^32. No microsecond precision is implied. */
uint32_t timer_now_ms(void);
uint32_t timer_resolution_ms(void);
/* Valid for intervals shorter than one full counter wrap. */
static inline uint32_t timer_elapsed_ms(uint32_t start, uint32_t end) { return end - start; }

#endif
