#include "master_internal.h"

#include "iolinki/crc.h"
#include "iolinki/frame.h"
#include "iolinki/protocol.h"

#include <string.h>

static bool iolink_master_send_full(iolink_master_port_t* port, const uint8_t* data, size_t len)
{
    int sent = port->phy->send(data, len);

    if(sent == (int)len)
    {
        return true;
    }

    port->send_errors++;
    port->state = IOLINK_MASTER_STATE_ERROR;
    return false;
}

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
    port->pd_out_len = config->pd_out_len;
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
    int frame_len;
    size_t od_pos;

    if((port == NULL) || (port->phy == NULL) || (port->phy->send == NULL))
    {
        return;
    }

    if(port->state == IOLINK_MASTER_STATE_STARTUP)
    {
        if(port->startup_step == 0U)
        {
            port->tx_buf[0] = 0x55U;
            if(iolink_master_send_full(port, port->tx_buf, 1U))
            {
                port->startup_step++;
            }
            return;
        }

        if(port->startup_step == 1U)
        {
            frame_len = iolink_frame_encode_type0(0x00U, port->tx_buf, sizeof(port->tx_buf));
            if(frame_len > 0)
            {
                if(iolink_master_send_full(port, port->tx_buf, (size_t)frame_len))
                {
                    port->startup_step++;
                    port->state = IOLINK_MASTER_STATE_PREOPERATE;
                }
            }
            return;
        }
    }

    if(port->state == IOLINK_MASTER_STATE_PREOPERATE)
    {
        frame_len = iolink_frame_encode_type0(IOLINK_MC_TRANSITION_COMMAND,
                                              port->tx_buf,
                                              sizeof(port->tx_buf));
        if(frame_len > 0)
        {
            if(iolink_master_send_full(port, port->tx_buf, (size_t)frame_len))
            {
                port->startup_step++;
                port->state = IOLINK_MASTER_STATE_OPERATE;
            }
        }
        return;
    }

    if(port->state == IOLINK_MASTER_STATE_OPERATE)
    {
        frame_len = iolink_frame_encode_type1_cycle(port->pd_out,
                                                    port->pd_out_len,
                                                    port->od_len,
                                                    port->tx_buf,
                                                    sizeof(port->tx_buf));
        if(frame_len > 0)
        {
            od_pos = (size_t)IOLINK_M_SEQ_HEADER_LEN + port->pd_out_len;
            iolink_master_isdu_fill_od(port, &port->tx_buf[od_pos], port->od_len);
            port->tx_buf[frame_len - 1] = iolink_crc6(port->tx_buf, (uint8_t)(frame_len - 1));

            if(iolink_master_send_full(port, port->tx_buf, (size_t)frame_len))
            {
                port->cycle_count++;
            }
        }
    }
}

int iolink_master_on_rx(iolink_master_port_t* port, const uint8_t* data, uint8_t len)
{
    iolink_frame_operate_response_t resp;

    if((port == NULL) || (data == NULL) || (len == 0U))
    {
        return -1;
    }

    if(iolink_frame_decode_operate_response(data,
                                            len,
                                            port->config.pd_in_len,
                                            port->od_len,
                                            &resp) != 0)
    {
        return -2;
    }

    if(!resp.checksum_ok)
    {
        port->checksum_errors++;
        return -3;
    }

    if(resp.pd_valid)
    {
        memcpy(port->pd_in, resp.pd, resp.pd_len);
        port->pd_in_len = resp.pd_len;
        port->pd_valid = true;
    }

    iolink_master_isdu_on_od(port, resp.od, resp.od_len);

    return 0;
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

int iolink_master_set_pd_out(iolink_master_port_t* port, const uint8_t* data, uint8_t len)
{
    if((port == NULL) || ((data == NULL) && (len > 0U)))
    {
        return -1;
    }

    if((len > IOLINK_PD_OUT_MAX_SIZE) || (len != port->config.pd_out_len))
    {
        return -2;
    }

    if(len > 0U)
    {
        memcpy(port->pd_out, data, len);
    }
    port->pd_out_len = len;
    return 0;
}
