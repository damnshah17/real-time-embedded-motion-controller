#include "platform/stm32/tiny_format.h"
#include "test_check.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static int format(char *out, size_t size, const char *pattern, ...)
{
    va_list args; va_start(args, pattern);
    const int count = tiny_vformat(out, size, pattern, args);
    va_end(args); return count;
}
int main(void)
{
    char out[128];
    CHECK(format(out, sizeof(out), "%06u %u %d %lu %ld %s", 7U, UINT32_MAX, INT32_MIN,
                 (unsigned long)UINT32_MAX, (long)INT32_MIN, "OK") > 0);
    CHECK(strcmp(out, "000007 4294967295 -2147483648 4294967295 -2147483648 OK") == 0);
    CHECK(format(out, 4U, "%s", "abc") == 3 && strcmp(out, "abc") == 0);
    CHECK(format(out, 3U, "%s", "abc") == -1);
    CHECK(format(out, 1U, "x") == -1 && out[0] == '\0');
    CHECK(format(out, 0U, "x") == -1);
    CHECK(format(out, sizeof(out), "%") == -1);
    CHECK(format(out, sizeof(out), "%.6") == -1);
    CHECK(format(out, sizeof(out), "%f", 1.0) == -1);
    const float numbers[] = {0.0f, -0.0f, 0.8f, -25.0f, 9.999999f, FLT_MAX, -FLT_MAX, FLT_MIN, FLT_TRUE_MIN};
    for (unsigned i = 0U; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        CHECK(format(out, sizeof(out), "%.6g", (double)numbers[i]) > 0);
        const double parsed = strtod(out, NULL);
        CHECK(isfinite(parsed));
        CHECK(numbers[i] == 0.0f ? parsed == 0.0 : fabs(parsed / (double)numbers[i] - 1.0) < 0.00001);
    }
    CHECK(format(out, sizeof(out), "%.6g", (double)NAN) == 7 && strcmp(out,"invalid") == 0);
    CHECK(format(out, sizeof(out), "%.6g", (double)INFINITY) == 7 && strcmp(out,"invalid") == 0);
    return 0;
}
