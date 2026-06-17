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
    iolink_master_tick_event_t event;

    if(controller == NULL)
    {
        return -1;
    }

    state = iolink_master_controller_state(controller);
    for(i = 0U; i < state->port_count; i++)
    {
        event = ((response_timeouts != NULL) && response_timeouts[i])
                    ? IOLINK_MASTER_TICK_RESPONSE_TIMEOUT
                    : IOLINK_MASTER_TICK_CYCLE_DUE;
        ret = iolink_master_tick_event(&state->ports[i], event);
        if((ret < 0) && (first_error == 0))
        {
            first_error = ret;
        }
    }

    return first_error;
}

int iolink_master_controller_tick_events(iolink_master_controller_t* controller,
                                         const iolink_master_tick_event_t* events)
{
    iolink_master_controller_state_t* state;
    uint8_t i;
    int ret;
    int first_error = 0;
    iolink_master_tick_event_t event;

    if(controller == NULL)
    {
        return -1;
    }

    state = iolink_master_controller_state(controller);
    for(i = 0U; i < state->port_count; i++)
    {
        event = (events != NULL) ? events[i] : IOLINK_MASTER_TICK_CYCLE_DUE;
        ret = iolink_master_tick_event(&state->ports[i], event);
        if((ret < 0) && (first_error == 0))
        {
            first_error = ret;
        }
    }

    return first_error;
}

int iolink_master_controller_tick_at(iolink_master_controller_t* controller, uint32_t now_100us)
{
    iolink_master_controller_state_t* state;
    uint8_t i;
    int ret;
    int first_error = 0;

    if(controller == NULL)
    {
        return -1;
    }

    state = iolink_master_controller_state(controller);
    for(i = 0U; i < state->port_count; i++)
    {
        ret = iolink_master_tick_at(&state->ports[i],
                                    iolink_master_response_due_at(&state->ports[i], now_100us)
                                        ? IOLINK_MASTER_TICK_RESPONSE_TIMEOUT
                                        : IOLINK_MASTER_TICK_CYCLE_DUE,
                                    now_100us);
        if((ret < 0) && (first_error == 0))
        {
            first_error = ret;
        }
    }

    return first_error;
}
