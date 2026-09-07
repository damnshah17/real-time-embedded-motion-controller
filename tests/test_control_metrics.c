#include "control_metrics.h"
#include "test_check.h"
int main(void)
{
    const motion_config_t c = motion_default_config();
    for (unsigned int direction = 0U; direction < 2U; ++direction) {
        control_metrics_t m;
        control_metrics_init(&m, direction == 0U ? 900 : 1100, 1000);
        motion_snapshot_t s = {.state = APP_MOVING, .position = 1000, .pwm = -0.8f};
        control_metrics_sample(&m, &s, &c, 100U);
        CHECK(m.in_band && m.settling_ms == 100U && !m.completed);
        s.position = direction == 0U ? 1010 : 990;
        control_metrics_sample(&m, &s, &c, 200U);
        CHECK(!m.in_band && m.overshoot == 10.0f);
        s.position = 1001; s.velocity = 100.0f;
        control_metrics_sample(&m, &s, &c, 300U);
        CHECK(!m.in_band);
        s.velocity = 0.0f;
        control_metrics_sample(&m, &s, &c, 400U);
        s.state = APP_IDLE; s.pwm = 0.0f;
        control_metrics_sample(&m, &s, &c, 600U);
        CHECK(m.settling_ms == 400U && m.completion_ms == 600U && m.completed);
        CHECK(m.final_error == 1.0f && m.maximum_pwm == 0.8f);
    }
    return EXIT_SUCCESS;
}
