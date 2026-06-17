#include "iolinki_master/master.h"

#include <string.h>

#ifdef IOLINK_H
#error "iolinki_master/master.h must not include the aggregate iolinki/iolink.h device header"
#endif

int main(void)
{
    iolink_master_port_t port;
    iolink_master_controller_t controller;

    memset(&port, 0, sizeof(port));
    memset(&controller, 0, sizeof(controller));

    return 0;
}
