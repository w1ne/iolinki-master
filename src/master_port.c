#include "master_internal.h"

#include <string.h>

int iolink_master_init(iolink_master_port_t* port,
                       const iolink_phy_api_t* phy,
                       const iolink_master_config_t* config)
{
    int ret;

    if((port == NULL) || (phy == NULL) || (config == NULL) ||
       (config->pd_in_len > IOLINK_PD_IN_MAX_SIZE) ||
       (config->pd_out_len > IOLINK_PD_OUT_MAX_SIZE))
    {
        return -1;
    }

    memset(port, 0, sizeof(*port));
    port->phy = phy;
    port->config = *config;
    port->od_len = iolink_master_od_len_for_type(config->m_seq_type);
    port->pd_in_len = config->pd_in_len;
    port->state = IOLINK_MASTER_STATE_STARTUP;

    if(phy->init != NULL)
    {
        ret = phy->init();
        if(ret != 0)
        {
            port->state = IOLINK_MASTER_STATE_ERROR;
            return ret;
        }
    }

    if(phy->set_baudrate != NULL)
    {
        phy->set_baudrate(config->baudrate);
    }

    if(phy->set_mode != NULL)
    {
        phy->set_mode(IOLINK_PHY_MODE_SDCI);
    }

    return 0;
}

void iolink_master_process(iolink_master_port_t* port)
{
    (void)port;
}

iolink_master_state_t iolink_master_get_state(const iolink_master_port_t* port)
{
    if(port == NULL)
    {
        return IOLINK_MASTER_STATE_ERROR;
    }

    return port->state;
}

int iolink_master_get_pd_in(const iolink_master_port_t* port,
                            uint8_t* buffer,
                            uint8_t buffer_len,
                            uint8_t* out_len)
{
    if((port == NULL) || (buffer == NULL) || (out_len == NULL))
    {
        return -1;
    }

    if(buffer_len < port->pd_in_len)
    {
        *out_len = port->pd_in_len;
        return -2;
    }

    *out_len = port->pd_in_len;

    if(!port->pd_valid)
    {
        return 1;
    }

    memcpy(buffer, port->pd_in, port->pd_in_len);
    return 0;
}
