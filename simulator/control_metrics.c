#include "control_metrics.h"
#include <math.h>
#include <string.h>

void control_metrics_init(control_metrics_t *m, int32_t start, int32_t target)
{
    *m = (control_metrics_t){.start = start, .target = target,
                           .output_hash = UINT64_C(14695981039346656037)};
}
static void hash_value(control_metrics_t *m, uint32_t value)
{
    for (unsigned int i = 0U; i < 4U; ++i) {
        m->output_hash ^= value & 255U;
        m->output_hash *= UINT64_C(1099511628211);
        value >>= 8U;
    }
}
void control_metrics_sample(control_metrics_t *m, const motion_snapshot_t *s,
                            const motion_config_t *c, uint32_t elapsed_ms)
{
    m->elapsed_ms = elapsed_ms;
    m->final_position = s->position;
    m->final_error = fabsf((float)((int64_t)s->position - m->target));
    const float beyond = m->target > m->start ? (float)((int64_t)s->position - m->target) :
                         (m->target < m->start ? (float)((int64_t)m->target - s->position) : 0.0f);
    if (beyond > m->overshoot) { m->overshoot = beyond; }
    if (fabsf(s->pwm) > m->maximum_pwm) { m->maximum_pwm = fabsf(s->pwm); }
    const bool band = m->final_error <= c->tolerance && fabsf(s->velocity) <= c->completion_velocity;
    if (band && !m->in_band) { m->settling_ms = elapsed_ms; }
    m->in_band = band;
    if (!m->completed && s->state == APP_IDLE) { m->completed = true; m->completion_ms = elapsed_ms; }
    uint32_t bits;
    _Static_assert(sizeof(bits) == sizeof(s->pwm), "Trace hashing requires 32-bit float");
    memcpy(&bits, &s->pwm, sizeof(bits));
    hash_value(m, bits);
    hash_value(m, (uint32_t)s->position);
    hash_value(m, (uint32_t)s->state);
    hash_value(m, s->stable_cycles);
}
