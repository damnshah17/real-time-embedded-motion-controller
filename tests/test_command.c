#include "protocol/command.h"
#include "test_check.h"
#include <string.h>

int main(void)
{
    static const struct {
        const char *input;
        command_parse_result_t result;
        command_type_t type;
        int32_t value;
    } cases[] = {
        {"STATUS", PARSE_OK, CMD_STATUS, 0}, {" HELP\r\n", PARSE_OK, CMD_HELP, 0},
        {"MOVE 1000", PARSE_OK, CMD_MOVE_ABS, 1000}, {"MOVE_REL -25", PARSE_OK, CMD_MOVE_REL, -25},
        {"SPEED +500", PARSE_OK, CMD_SET_SPEED, 500}, {"HOME", PARSE_OK, CMD_HOME, 0},
        {"STOP", PARSE_OK, CMD_STOP, 0}, {"ESTOP", PARSE_OK, CMD_ESTOP, 0}, {"RESET", PARSE_OK, CMD_RESET, 0},
        {"MOVE -2147483648", PARSE_OK, CMD_MOVE_ABS, INT32_MIN},
        {"MOVE 2147483647", PARSE_OK, CMD_MOVE_ABS, INT32_MAX},
        {"MOVE 2147483648", PARSE_ARGUMENT, CMD_STATUS, 0},
        {"MOVE -2147483649", PARSE_ARGUMENT, CMD_STATUS, 0},
        {"MOVE 99999999999999999999999999", PARSE_ARGUMENT, CMD_STATUS, 0},
        {"MOVE --2", PARSE_ARGUMENT, CMD_STATUS, 0}, {"MOVE +", PARSE_ARGUMENT, CMD_STATUS, 0},
        {"MOVE", PARSE_ARGUMENT, CMD_STATUS, 0}, {"MOVE 1 2", PARSE_ARGUMENT, CMD_STATUS, 0},
        {"MOVE bad", PARSE_ARGUMENT, CMD_STATUS, 0}, {"MOVE 1.5", PARSE_ARGUMENT, CMD_STATUS, 0},
        {"HOME 1", PARSE_ARGUMENT, CMD_STATUS, 0}, {"", PARSE_EMPTY, CMD_STATUS, 0},
        {" \t\r\n", PARSE_EMPTY, CMD_STATUS, 0}, {"MOVE_RELATIVE 2", PARSE_UNKNOWN, CMD_STATUS, 0},
        {"status", PARSE_UNKNOWN, CMD_STATUS, 0},
        {"SIM ESTOP ON", PARSE_UNKNOWN, CMD_STATUS, 0},
        {"SIM ENCODER FREEZE ON", PARSE_UNKNOWN, CMD_STATUS, 0}
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        motion_command_t command = {CMD_ESTOP, 123};
        CHECK(command_parse(cases[i].input, strlen(cases[i].input), &command) == cases[i].result);
        if (cases[i].result == PARSE_OK) {
            CHECK(command.type == cases[i].type && command.value == cases[i].value);
        } else { CHECK(command.type == CMD_ESTOP && command.value == 123); }
    }
    const char bounded[] = {'S','T','A','T','U','S'};
    motion_command_t command;
    CHECK(command_parse(bounded, sizeof(bounded), &command) == PARSE_OK);
    char maximum[COMMAND_LINE_CAPACITY];
    memset(maximum, ' ', sizeof(maximum));
    memcpy(maximum, "STATUS", 6U);
    CHECK(command_parse(maximum, sizeof(maximum) - 1U, &command) == PARSE_OK);
    CHECK(command_parse(maximum, sizeof(maximum), &command) == PARSE_TOO_LONG);
    const char embedded_nul[] = {'M','O','V','E',' ','1','\0','2'};
    CHECK(command_parse(embedded_nul, sizeof(embedded_nul), &command) == PARSE_ARGUMENT);
    return EXIT_SUCCESS;
}
