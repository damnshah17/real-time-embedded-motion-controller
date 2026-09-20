#include "platform/stm32/tiny_format.h"
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

typedef struct { char *data; size_t used, capacity; bool failed; } writer_t;
static void character(writer_t *w, char c)
{
    if (w->used + 1U >= w->capacity) { w->failed = true; return; }
    w->data[w->used++] = c;
}
static void string(writer_t *w, const char *s)
{
    if (s == NULL) { w->failed = true; return; }
    while (*s != '\0' && !w->failed) { character(w, *s++); }
}
static void integer(writer_t *w, uint32_t value, unsigned width)
{
    char digits[10]; unsigned n = 0U;
    do { digits[n++] = (char)('0' + value % 10U); value /= 10U; } while (value != 0U);
    while (width > n) { character(w, '0'); --width; }
    while (n != 0U) { character(w, digits[--n]); }
}
static void real(writer_t *w, double value)
{
    if (!isfinite(value)) { string(w, "invalid"); return; }
    if (value == 0.0) { character(w, '0'); return; }
    if (value < 0.0) { character(w, '-'); value = -value; }
    int exponent = 0;
    /* Double intermediates avoid accumulating a displayed-digit error at FLT_MAX. */
    while (value >= 10.0) { value /= 10.0; ++exponent; }
    while (value < 1.0) { value *= 10.0; --exponent; }
    uint32_t scaled = (uint32_t)(value * 100000.0 + 0.5);
    if (scaled >= 1000000U) { scaled = 100000U; ++exponent; }
    integer(w, scaled / 100000U, 1U); character(w, '.');
    integer(w, scaled % 100000U, 5U);
    character(w, 'e'); character(w, exponent < 0 ? '-' : '+');
    integer(w, (uint32_t)(exponent < 0 ? -exponent : exponent), 2U);
}
int tiny_vformat(char *out, size_t capacity, const char *format, va_list args)
{
    if (out == NULL || capacity == 0U || format == NULL) { return -1; }
    writer_t w = {out, 0U, capacity, false};
    while (*format != '\0' && !w.failed) {
        if (*format != '%') { character(&w, *format++); continue; }
        ++format; unsigned width = 0U;
        if (*format == '0') {
            ++format;
            if (*format != '6') { w.failed = true; break; }
            width = 6U; ++format;
        }
        if (format[0] == '.' && format[1] == '6' && format[2] == 'g') {
            real(&w, va_arg(args, double)); format += 3; continue;
        }
        bool long_value = *format == 'l';
        if (long_value) { ++format; }
        switch (*format) {
        case 's': if (long_value || width != 0U) { w.failed = true; }
                  else { string(&w, va_arg(args, const char *)); } break;
        case 'u': integer(&w, long_value ? (uint32_t)va_arg(args, unsigned long) : (uint32_t)va_arg(args, unsigned int), width); break;
        case 'd': {
            const int32_t value = long_value ? (int32_t)va_arg(args, long) : (int32_t)va_arg(args, int);
            if (value < 0) { character(&w, '-'); }
            integer(&w, value < 0 ? 0U - (uint32_t)value : (uint32_t)value, width); break;
        }
        default: w.failed = true; break;
        }
        if (*format != '\0') { ++format; }
    }
    out[w.used] = '\0'; return w.failed ? -1 : (int)w.used;
}
