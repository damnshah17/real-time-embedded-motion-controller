#ifndef MOTION_COMMAND_BUS_H
#define MOTION_COMMAND_BUS_H

#include "FreeRTOS.h"
#include "queue.h"
#include "protocol/command.h"
#include <stdbool.h>

#define COMMAND_QUEUE_CAPACITY 8U
typedef struct {
    QueueHandle_t queue;
    StaticQueue_t control;
    uint8_t storage[COMMAND_QUEUE_CAPACITY * sizeof(motion_command_t)];
    uint32_t sent;
    uint32_t received;
    uint32_t rejected;
    uint32_t high_water;
} command_bus_t;

bool command_bus_init(command_bus_t *bus);
/* Task-context, nonblocking; queue owns a copy after successful send. */
bool command_bus_send(command_bus_t *bus, const motion_command_t *command);
bool command_bus_receive(command_bus_t *bus, motion_command_t *command);

#endif
