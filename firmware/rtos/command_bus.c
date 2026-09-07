#include "rtos/command_bus.h"
#include "task.h"

bool command_bus_init(command_bus_t *bus)
{
    bus->sent = bus->received = bus->rejected = bus->high_water = 0U;
    bus->queue = xQueueCreateStatic(COMMAND_QUEUE_CAPACITY, sizeof(motion_command_t),
                                    bus->storage, &bus->control);
    return bus->queue != NULL;
}

bool command_bus_send(command_bus_t *bus, const motion_command_t *command)
{
    /* Keep the send and high-water observation indivisible to the consumer. */
    taskENTER_CRITICAL();
    const bool accepted = xQueueSend(bus->queue, command, 0U) == pdPASS;
    if (accepted) {
        ++bus->sent;
        const uint32_t used = (uint32_t)uxQueueMessagesWaiting(bus->queue);
        if (used > bus->high_water) { bus->high_water = used; }
    } else { ++bus->rejected; }
    taskEXIT_CRITICAL();
    return accepted;
}

bool command_bus_receive(command_bus_t *bus, motion_command_t *command)
{
    taskENTER_CRITICAL();
    const bool received = xQueueReceive(bus->queue, command, 0U) == pdPASS;
    if (received) { ++bus->received; }
    taskEXIT_CRITICAL();
    return received;
}
