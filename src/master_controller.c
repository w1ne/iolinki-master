#include "master_internal.h"

#include <string.h>

int iolink_master_controller_init(iolink_master_controller_t* controller,
                                  iolink_master_port_t* ports,
                                  uint8_t port_count,
                                  const iolink_phy_api_t* phys,
                                  const iolink_master_config_t* configs)
{
    iolink_master_controller_state_t* state;
    uint8_t i;
    int ret;

    if((controller == NULL) || (ports == NULL) || (port_count == 0U) || (phys == NULL) ||
       (configs == NULL))
    {
        return -1;
    }

    memset(controller, 0, sizeof(*controller));
    state = iolink_master_controller_state(controller);
    state->ports = ports;
    state->port_count = port_count;

    for(i = 0U; i < port_count; i++)
    {
        ret = iolink_master_init(&ports[i], &phys[i], &configs[i]);
        if(ret != 0)
        {
            state->port_count = i;
            return ret;
        }
    }

    return 0;
}

int iolink_master_controller_tick(iolink_master_controller_t* controller,
                                  const bool* response_timeouts)
{
    iolink_master_controller_state_t* state;
    uint8_t i;
    int ret;
    int first_error = 0;
    bool timeout;

    if(controller == NULL)
    {
        return -1;
    }

    state = iolink_master_controller_state(controller);
    for(i = 0U; i < state->port_count; i++)
    {
        timeout = (response_timeouts != NULL) ? response_timeouts[i] : false;
        ret = iolink_master_tick(&state->ports[i], timeout);
        if((ret < 0) && (first_error == 0))
        {
            first_error = ret;
        }
    }

    return first_error;
}
