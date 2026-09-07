#ifndef MOTION_COMMAND_H
#define MOTION_COMMAND_H

#include <stddef.h>
#include <stdint.h>

#define COMMAND_LINE_CAPACITY 64U
typedef enum {
    CMD_MOVE_ABS, CMD_MOVE_REL, CMD_SET_SPEED, CMD_HOME, CMD_STOP,
    CMD_ESTOP, CMD_RESET, CMD_STATUS, CMD_HELP
} command_type_t;

typedef struct {
    command_type_t type;
    int32_t value;
} motion_command_t;

typedef enum {
    PARSE_OK, PARSE_EMPTY, PARSE_UNKNOWN, PARSE_ARGUMENT, PARSE_TOO_LONG
} command_parse_result_t;

/* Length-delimited input, no NUL terminator required. Output unchanged on error. */
command_parse_result_t command_parse(const char *text, size_t length, motion_command_t *out);
const char *command_name(command_type_t type);
const char *command_parse_error(command_parse_result_t result);

#endif
