#ifndef MOTION_TINY_FORMAT_H
#define MOTION_TINY_FORMAT_H
#include <stdarg.h>
#include <stddef.h>
/* Only %s, %u, %d, %lu, %ld, %06u/%06lu and %.6g used by telemetry.
 * Returns -1 on unsupported format or insufficient capacity; never truncates successfully. */
int tiny_vformat(char *out, size_t capacity, const char *format, va_list args);
#endif
