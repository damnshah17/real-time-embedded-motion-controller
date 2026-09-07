#include "protocol/command.h"

#include <stdbool.h>
#include <string.h>

static const struct {
    const char *name;
    command_type_t type;
    bool argument;
} commands[] = {
    {"MOVE", CMD_MOVE_ABS, true}, {"MOVE_REL", CMD_MOVE_REL, true},
    {"SPEED", CMD_SET_SPEED, true}, {"HOME", CMD_HOME, false},
    {"STOP", CMD_STOP, false}, {"ESTOP", CMD_ESTOP, false},
    {"RESET", CMD_RESET, false}, {"STATUS", CMD_STATUS, false},
    {"HELP", CMD_HELP, false}
};

static bool whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

command_parse_result_t command_parse(const char *text, size_t length, motion_command_t *out)
{
    if (length >= COMMAND_LINE_CAPACITY) { return PARSE_TOO_LONG; }
    size_t start = 0U;
    while (start < length && whitespace(text[start])) { ++start; }
    while (length > start && whitespace(text[length - 1U])) { --length; }
    if (start == length) { return PARSE_EMPTY; }
    size_t end = start;
    while (end < length && !whitespace(text[end])) { ++end; }
    for (size_t i = 0U; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (strlen(commands[i].name) != end - start ||
            memcmp(text + start, commands[i].name, end - start) != 0) { continue; }
        motion_command_t parsed = {commands[i].type, 0};
        size_t pos = end;
        while (pos < length && whitespace(text[pos])) { ++pos; }
        if (!commands[i].argument) {
            if (pos != length) { return PARSE_ARGUMENT; }
        } else {
            bool negative = false;
            if (pos < length && (text[pos] == '-' || text[pos] == '+')) {
                negative = text[pos++] == '-';
            }
            if (pos == length) { return PARSE_ARGUMENT; }
            const uint32_t limit = negative ? UINT32_C(2147483648) : INT32_MAX;
            uint32_t value = 0U;
            while (pos < length) {
                if (text[pos] < '0' || text[pos] > '9') { return PARSE_ARGUMENT; }
                const uint32_t digit = (uint32_t)(text[pos++] - '0');
                if (value > (limit - digit) / 10U) { return PARSE_ARGUMENT; }
                value = value * 10U + digit;
            }
            parsed.value = negative ? (value == UINT32_C(2147483648) ? INT32_MIN : -(int32_t)value)
                                    : (int32_t)value;
        }
        *out = parsed;
        return PARSE_OK;
    }
    return PARSE_UNKNOWN;
}

const char *command_name(command_type_t type)
{
    for (size_t i = 0U; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (commands[i].type == type) { return commands[i].name; }
    }
    return "UNKNOWN";
}

const char *command_parse_error(command_parse_result_t result)
{
    switch (result) {
    case PARSE_OK: return "OK";
    case PARSE_EMPTY: return "EMPTY";
    case PARSE_UNKNOWN: return "UNKNOWN_COMMAND";
    case PARSE_ARGUMENT: return "INVALID_ARGUMENT";
    case PARSE_TOO_LONG: return "LINE_TOO_LONG";
    default: return "INTERNAL";
    }
}
