#include "drivers/encoder.h"
#include "platform/host/encoder_host.h"
#include "platform/host/host_access.h"
#include <stddef.h>

static bool initialized;
static uint32_t raw_count;
static uint32_t reference_offset;
static int32_t signed_bits(uint32_t bits)
{
    return bits <= INT32_MAX ? (int32_t)bits : INT32_MIN + (int32_t)(bits - UINT32_C(2147483648));
}

void encoder_init(void) { raw_count = 0U; reference_offset = 0U; initialized = true; }
bool encoder_get_count(int32_t *count)
{
    if (count == NULL) { return false; }
    host_access_enter();
    const bool ready = initialized;
    if (ready) {
        const uint32_t bits = raw_count + reference_offset;
        *count = signed_bits(bits);
    }
    host_access_leave();
    return ready;
}
bool encoder_set_reference(int32_t count)
{
    host_access_enter();
    const bool ready = initialized;
    if (ready) { reference_offset = (uint32_t)count - raw_count; }
    host_access_leave();
    return ready;
}
bool encoder_host_set_count(int32_t count)
{
    host_access_enter();
    const bool ready = initialized;
    if (ready) { raw_count = (uint32_t)count; }
    host_access_leave();
    return ready;
}
bool encoder_host_get_state(encoder_host_state_t *state)
{
    if (state == NULL) { return false; }
    host_access_enter();
    const bool ready = initialized;
    if (ready) { *state = (encoder_host_state_t){signed_bits(raw_count), signed_bits(reference_offset)}; }
    host_access_leave();
    return ready;
}
