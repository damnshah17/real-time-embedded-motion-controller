#include "control/pid.h"
#include "test_check.h"
#include <float.h>
#include <math.h>
#include <string.h>

static pid_config_t config(void)
{ return (pid_config_t){.kp = 2.0f, .output_min = -10.0f, .output_max = 10.0f, .integral_min = -2.0f, .integral_max = 2.0f}; }
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    pid_config_t c = config();
    pid_controller_t pid = {0};
    float output = 0.0f;
    CHECK(pid_init(&pid, &c));
    if (strcmp(argv[1], "zero") == 0) {
        CHECK(pid_update(&pid, 50.0f, 50.0f, 0.01f, &output) && output == 0.0f);
    } else if (strcmp(argv[1], "proportional") == 0) {
        CHECK(pid_update(&pid, 3.0f, 0.0f, 0.01f, &output) && output == 6.0f);
        CHECK(pid_update(&pid, -3.0f, 0.0f, 0.1f, &output) && output == -6.0f);
    } else if (strcmp(argv[1], "integral") == 0) {
        c.kp = 0.0f; c.ki = 0.5f; CHECK(pid_init(&pid, &c));
        CHECK(pid_update(&pid, 1.0f, 0.0f, 1.0f, &output) && output == 0.5f);
        CHECK(pid_update(&pid, 1.0f, 0.0f, 1.0f, &output) && output == 1.0f);
        CHECK(pid_update(&pid, -1.0f, 0.0f, 1.0f, &output) && output == 0.5f);
    } else if (strcmp(argv[1], "derivative") == 0) {
        c.kp = 0.0f; c.kd = 1.0f; CHECK(pid_init(&pid, &c));
        CHECK(pid_update(&pid, 10.0f, 0.0f, 1.0f, &output) && output == 0.0f);
        CHECK(pid_update(&pid, 10.0f, 2.0f, 1.0f, &output) && output == -2.0f);
        CHECK(pid_update(&pid, 100.0f, 2.0f, 1.0f, &output) && output == 0.0f); /* No setpoint kick. */
    } else if (strcmp(argv[1], "saturation") == 0) {
        CHECK(pid_update(&pid, 100.0f, 0.0f, 0.01f, &output) && output == 10.0f);
        CHECK(pid_update(&pid, -100.0f, 0.0f, 0.01f, &output) && output == -10.0f);
    } else if (strcmp(argv[1], "windup") == 0) {
        c.ki = 1.0f; CHECK(pid_init(&pid, &c));
        for (unsigned int i = 0U; i < 10000U; ++i) {
            CHECK(pid_update(&pid, 1000.0f, 0.0f, 0.01f, &output));
            CHECK(pid.integral <= 2.0f && output <= 10.0f);
        }
        CHECK(pid.integral == 2.0f);
        CHECK(pid_update(&pid, -1.0f, 0.0f, 1.0f, &output) && pid.integral == 1.0f);
        CHECK(pid_update(&pid, -1000.0f, 0.0f, 1.0f, &output) && pid.integral == -2.0f);
    } else if (strcmp(argv[1], "reset") == 0) {
        c.ki = c.kd = 1.0f; CHECK(pid_init(&pid, &c));
        CHECK(pid_update(&pid, 10.0f, 2.0f, 0.1f, &output));
        pid_reset(&pid);
        CHECK(pid.integral == 0.0f && !pid.sampled && pid.initialized);
        CHECK(pid_update(&pid, 100.0f, 100.0f, 0.1f, &output) && output == 0.0f);
    } else if (strcmp(argv[1], "determinism") == 0) {
        c.ki = 0.3f; c.kd = 0.1f; CHECK(pid_init(&pid, &c));
        float first[100];
        for (unsigned int run = 0U; run < 2U; ++run) {
            pid_reset(&pid);
            for (unsigned int i = 0U; i < 100U; ++i) {
                CHECK(pid_update(&pid, 10.0f, (float)i / 10.0f, 0.01f, &output));
                if (run == 0U) { first[i] = output; } else { CHECK(output == first[i]); }
            }
        }
    } else if (strcmp(argv[1], "invalid") == 0) {
        output = 7.0f;
        CHECK(!pid_update(NULL, 0.0f, 0.0f, 1.0f, &output));
        CHECK(!pid_update(&pid, NAN, 0.0f, 1.0f, &output));
        CHECK(!pid_update(&pid, 0.0f, INFINITY, 1.0f, &output));
        CHECK(!pid_update(&pid, 1.0f, 0.0f, 0.0f, &output));
        CHECK(!pid_update(&pid, 1.0f, 0.0f, -1.0f, &output));
        CHECK(!pid_update(&pid, 1.0f, 0.0f, NAN, &output));
        CHECK(!pid_update(&pid, FLT_MAX, -FLT_MAX, 1.0f, &output));
        CHECK(!pid_update(&pid, 1.0f, 0.0f, 1.0f, NULL));
        CHECK(output == 7.0f && pid.integral == 0.0f && !pid.sampled);
        c.kp = NAN; CHECK(!pid_init(&pid, &c));
        c = config(); c.output_min = c.output_max; CHECK(!pid_init(&pid, &c));
        c = config(); c.integral_min = 1.0f; CHECK(!pid_init(&pid, &c));
    } else { CHECK(false); }
    return EXIT_SUCCESS;
}
