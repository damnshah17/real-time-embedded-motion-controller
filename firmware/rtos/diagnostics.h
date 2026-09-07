#ifndef MOTION_DIAGNOSTICS_H
#define MOTION_DIAGNOSTICS_H

#include "protocol/command.h"
#include "control/motion_controller.h"
#include <stdbool.h>

#define DIAGNOSTIC_CAPACITY 32U
#define DIAGNOSTIC_TEXT_CAPACITY 80U
typedef enum { DIAG_TEXT, DIAG_QUEUED, DIAG_QUEUE_FULL, DIAG_PARSE_ERROR, DIAG_RECEIVED, DIAG_MOTION } diagnostic_kind_t;
typedef struct {
    uint32_t timestamp_ms;
    diagnostic_kind_t kind;
    motion_command_t command;
    command_parse_result_t parse_result;
    motion_result_t motion_result;
    char text[DIAGNOSTIC_TEXT_CAPACITY];
} diagnostic_t;

bool diagnostics_init(void);
bool diagnostics_text(const char *text);
void diagnostics_command(diagnostic_kind_t kind, motion_command_t command, command_parse_result_t result);
void diagnostics_motion(motion_command_t command, motion_result_t result);
bool diagnostics_receive(diagnostic_t *record);
uint32_t diagnostics_dropped(void);

#endif
