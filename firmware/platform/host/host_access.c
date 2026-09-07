#include "platform/host/host_access.h"
#include <stddef.h>

static void (*enter_hook)(void);
static void (*leave_hook)(void);

bool host_access_configure(void (*enter)(void), void (*leave)(void))
{
    if ((enter == NULL) != (leave == NULL)) { return false; }
    enter_hook = enter;
    leave_hook = leave;
    return true;
}
void host_access_enter(void) { if (enter_hook != NULL) { enter_hook(); } }
void host_access_leave(void) { if (leave_hook != NULL) { leave_hook(); } }
