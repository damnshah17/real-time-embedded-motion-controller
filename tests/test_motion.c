#include "control/motion_controller.h"
#include "drivers/platform.h"
#include "drivers/motor.h"
#include "platform/host/encoder_host.h"
#include "test_check.h"
#include <math.h>
#include <string.h>

static motion_controller_t motion;
static int sample(int32_t count)
{
    CHECK(encoder_host_set_count(count));
    CHECK(motion_sample(&motion, 0.01f));
    CHECK(motion_update(&motion, 0.01f));
    return EXIT_SUCCESS;
}
int main(int argc, char **argv)
{
    CHECK(argc == 2 && platform_init());
    motion_config_t c = motion_default_config();
    CHECK(motion_init(&motion, &c) && sample(1000) == EXIT_SUCCESS);
    if (strcmp(argv[1], "stability") == 0) {
        CHECK(motion_command(&motion, (motion_command_t){CMD_MOVE_ABS, 1003}) == MOTION_ACCEPTED);
        CHECK(sample(1000) == EXIT_SUCCESS && motion.data.stable_cycles == 1U);
        CHECK(sample(1002) == EXIT_SUCCESS && motion.data.stable_cycles == 0U); /* Within position band, too fast. */
        CHECK(sample(1002) == EXIT_SUCCESS && motion.data.stable_cycles == 1U);
        CHECK(sample(1010) == EXIT_SUCCESS && motion.data.stable_cycles == 0U); /* Transient band exit. */
        CHECK(sample(1003) == EXIT_SUCCESS && motion.data.stable_cycles == 0U);
        for (uint32_t i = 1U; i < c.dwell_cycles; ++i) {
            CHECK(sample(1003) == EXIT_SUCCESS && motion.data.state == APP_MOVING);
        }
        CHECK(sample(1003) == EXIT_SUCCESS && motion.data.state == APP_IDLE);
        CHECK(motion.data.completed_moves == 1U && motor_get_commanded_output() == 0.0f);
    } else if (strcmp(argv[1], "velocity") == 0) {
        CHECK(motion.data.velocity == 0.0f);
        CHECK(sample(1002) == EXIT_SUCCESS && motion.data.velocity == 200.0f);
        CHECK(sample(999) == EXIT_SUCCESS && motion.data.velocity == -300.0f);
        CHECK(motion_init(&motion, &c) && sample(INT32_MAX) == EXIT_SUCCESS && motion.data.velocity == 0.0f);
        CHECK(sample(INT32_MIN) == EXIT_SUCCESS && motion.data.velocity == 100.0f);
        CHECK(sample(INT32_MAX) == EXIT_SUCCESS && motion.data.velocity == -100.0f);
        CHECK(motion_init(&motion, &c) && sample(0) == EXIT_SUCCESS);
        CHECK(encoder_host_set_count(INT32_MIN) && !motion_sample(&motion, 0.01f));
        CHECK(!motion.data.feedback_valid && motor_get_commanded_output() == 0.0f);
    } else if (strcmp(argv[1], "reset") == 0) {
        CHECK(motion_command(&motion, (motion_command_t){CMD_MOVE_ABS, 2000}) == MOTION_ACCEPTED);
        CHECK(sample(1000) == EXIT_SUCCESS && motion.pid.integral > 0.0f);
        CHECK(motion_command(&motion, (motion_command_t){CMD_STOP, 0}) == MOTION_ACCEPTED);
        for (uint32_t i = 0U; i < c.dwell_cycles; ++i) { CHECK(sample(1000) == EXIT_SUCCESS); }
        CHECK(motion.data.state == APP_IDLE && motion.data.completed_moves == 0U);
        CHECK(motion_command(&motion, (motion_command_t){CMD_MOVE_ABS, 500}) == MOTION_ACCEPTED);
        CHECK(motion.pid.integral == 0.0f && motion.data.stable_cycles == 0U);
        CHECK(sample(1000) == EXIT_SUCCESS);
        const float output = motion.data.pwm;
        CHECK(motion_init(&motion, &c) && sample(1000) == EXIT_SUCCESS);
        CHECK(motion_command(&motion, (motion_command_t){CMD_MOVE_ABS, 500}) == MOTION_ACCEPTED);
        CHECK(sample(1000) == EXIT_SUCCESS && motion.data.pwm == output);
    } else if (strcmp(argv[1], "invalid") == 0) {
        CHECK(motion_command(&motion, (motion_command_t){CMD_MOVE_REL, INT32_MIN}) == MOTION_OUT_OF_RANGE);
        CHECK(motion_command(&motion, (motion_command_t){CMD_MOVE_ABS, 2000}) == MOTION_ACCEPTED);
        CHECK(sample(1000) == EXIT_SUCCESS && motor_get_commanded_output() != 0.0f);
        CHECK(!motion_sample(&motion, 0.0f) && motor_get_commanded_output() == 0.0f);
        CHECK(motion.data.state == APP_IDLE && !motion.data.feedback_valid);
        CHECK(motion_command(&motion, (motion_command_t){CMD_MOVE_ABS, 1000}) == MOTION_INPUT_ERROR);
        CHECK(!motion_update(&motion, NAN));
        CHECK(sample(1000) == EXIT_SUCCESS); /* Recovery requires fresh feedback and a new command. */
        c.pid.output_max = 1.1f; CHECK(!motion_init(&motion, &c));
        c = motion_default_config(); c.dwell_cycles = 0U; CHECK(!motion_init(&motion, &c));
        c = motion_default_config(); c.tolerance = NAN; CHECK(!motion_init(&motion, &c));
    } else { CHECK(false); }
    return EXIT_SUCCESS;
}
