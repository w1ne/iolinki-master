#include "iolinki_master/master.h"

#include <string.h>

#ifdef IOLINK_H
#error "iolinki_master/master.h must not include the aggregate iolinki/iolink.h device header"
#endif

int main(void)
{
    iolink_master_port_t port;
    iolink_master_controller_t controller;
    iolink_master_tick_event_t event = IOLINK_MASTER_TICK_CYCLE_DUE;

    memset(&port, 0, sizeof(port));
    memset(&controller, 0, sizeof(controller));
    (void)iolink_master_tick_at(&port, event, 0U);
    (void)iolink_master_controller_tick_at(&controller, 0U);

    return 0;
}
