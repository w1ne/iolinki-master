#include "master_internal.h"

#include "iolinki/crc.h"
#include "iolinki/frame.h"
#include "iolinki/protocol.h"

#include <string.h>

static const iolink_baudrate_t g_iolink_master_baudrate_scan[] = {
    IOLINK_BAUDRATE_COM3,
    IOLINK_BAUDRATE_COM2,
    IOLINK_BAUDRATE_COM1,
};

static iolink_baudrate_t iolink_master_startup_baudrate(const iolink_master_port_t* port)
{
    if(port->config.auto_baudrate)
    {
        return g_iolink_master_baudrate_scan[port->startup.baudrate_index];
    }

    return port->config.baudrate;
}

static bool iolink_master_send_full(iolink_master_port_t* port, const uint8_t* data, size_t len)
{
    int sent = port->phy->send(data, len);

    if(sent == (int)len)
    {
        return true;
    }

    port->diagnostics.send_errors++;
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
       (config->pd_out_len > IOLINK_PD_OUT_MAX_SIZE) ||
       (config->m_seq_type > IOLINK_MASTER_M_SEQ_TYPE_2_V) ||
       (config->baudrate > IOLINK_BAUDRATE_COM3) ||
       (config->port_mode > IOLINK_MASTER_PORT_MODE_DEACTIVATED) ||
       ((config->m_seq_type == IOLINK_MASTER_M_SEQ_TYPE_0) &&
        ((config->pd_in_len > 0U) || (config->pd_out_len > 0U))))
    {
        return -1;
    }

    memset(port, 0, sizeof(*port));
    port->phy = phy;
    port->config = *config;
    port->od_len = iolink_master_od_len_for_type(config->m_seq_type);
    port->pd_in_len = config->pd_in_len;
    port->pd_out_len = config->pd_out_len;
    port->startup.baudrate_index = 0U;
    port->state = (config->port_mode == IOLINK_MASTER_PORT_MODE_IOLINK)
                      ? IOLINK_MASTER_STATE_STARTUP
                      : IOLINK_MASTER_STATE_INACTIVE;

    if(phy->init != NULL)
    {
        ret = phy->init();
        if(ret != 0)
        {
            port->state = IOLINK_MASTER_STATE_ERROR;
            return ret;
        }
    }

    if(config->port_mode == IOLINK_MASTER_PORT_MODE_DEACTIVATED)
    {
        if(phy->set_mode != NULL)
        {
            phy->set_mode(IOLINK_PHY_MODE_INACTIVE);
        }
        return 0;
    }

    if((config->port_mode == IOLINK_MASTER_PORT_MODE_DI) ||
       (config->port_mode == IOLINK_MASTER_PORT_MODE_DQ))
    {
        if(phy->set_mode != NULL)
        {
            phy->set_mode(IOLINK_PHY_MODE_SIO);
        }
        return 0;
    }

    if(phy->set_baudrate != NULL)
    {
        phy->set_baudrate(iolink_master_startup_baudrate(port));
    }

    if(phy->set_mode != NULL)
    {
        phy->set_mode(IOLINK_PHY_MODE_SDCI);
    }

    return 0;
}

int iolink_master_restart(iolink_master_port_t* port)
{
    const iolink_phy_api_t* phy;
    iolink_master_config_t config;

    if((port == NULL) || (port->phy == NULL))
    {
        return -1;
    }

    phy = port->phy;
    config = port->config;

    return iolink_master_init(port, phy, &config);
}

int iolink_master_on_timeout(iolink_master_port_t* port)
{
    if(port == NULL)
    {
        return -1;
    }

    if(port->state != IOLINK_MASTER_STATE_STARTUP)
    {
        if(port->state == IOLINK_MASTER_STATE_OPERATE)
        {
            if(port->diagnostics.rx_retry_count < 2U)
            {
                port->diagnostics.rx_retry_count++;
                return 1;
            }

            port->state = IOLINK_MASTER_STATE_ERROR;
            return -2;
        }

        return 0;
    }

    if(port->phy == NULL)
    {
        return -1;
    }

    if(port->config.auto_baudrate &&
       (port->startup.baudrate_index <
        (uint8_t)((sizeof(g_iolink_master_baudrate_scan) /
                   sizeof(g_iolink_master_baudrate_scan[0])) -
                  1U)))
    {
        port->startup.baudrate_index++;
        port->startup.step = 0U;
        if(port->phy->set_baudrate != NULL)
        {
            port->phy->set_baudrate(iolink_master_startup_baudrate(port));
        }
        return 1;
    }

    port->state = IOLINK_MASTER_STATE_ERROR;
    return -2;
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
        if(port->startup.step == 0U)
        {
            port->tx_buf[0] = 0x55U;
            if(iolink_master_send_full(port, port->tx_buf, 1U))
            {
                port->startup.step++;
            }
            return;
        }

        if(port->startup.step == 1U)
        {
            frame_len = iolink_frame_encode_type0(0x00U, port->tx_buf, sizeof(port->tx_buf));
            if(frame_len > 0)
            {
                if(iolink_master_send_full(port, port->tx_buf, (size_t)frame_len))
                {
                    port->startup.step++;
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
                port->startup.step++;
                port->state = IOLINK_MASTER_STATE_OPERATE;
            }
        }
        return;
    }

    if(port->state == IOLINK_MASTER_STATE_OPERATE)
    {
        if((port->config.m_seq_type == IOLINK_MASTER_M_SEQ_TYPE_0) &&
           (port->config.pd_in_len == 0U) && (port->pd_out_len == 0U))
        {
            uint8_t od = 0U;
            iolink_master_isdu_fill_od(port, &od, 1U);
            frame_len = iolink_frame_encode_type0(od, port->tx_buf, sizeof(port->tx_buf));
            if(frame_len > 0)
            {
                if(iolink_master_send_full(port, port->tx_buf, (size_t)frame_len))
                {
                    port->cycle_count++;
                }
            }
            return;
        }

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

int iolink_master_poll_rx(iolink_master_port_t* port)
{
    uint8_t byte;
    uint8_t expected_len;
    int recv_ret;
    int frame_ret;
    int frames = 0;

    if((port == NULL) || (port->phy == NULL) || (port->phy->recv_byte == NULL))
    {
        return -1;
    }

    if((port->state == IOLINK_MASTER_STATE_STARTUP) && (port->startup.step >= 2U))
    {
        expected_len = IOLINK_M_SEQ_TYPE0_LEN;
    }
    else if(port->state == IOLINK_MASTER_STATE_OPERATE)
    {
        expected_len = (uint8_t)(1U + port->config.pd_in_len + port->od_len + 1U);
    }
    else
    {
        return 0;
    }

    while((recv_ret = port->phy->recv_byte(&byte)) > 0)
    {
        if(port->rx.len >= sizeof(port->rx.buf))
        {
            port->state = IOLINK_MASTER_STATE_ERROR;
            port->rx.len = 0U;
            return -3;
        }

        port->rx.buf[port->rx.len++] = byte;

        if(port->rx.len >= expected_len)
        {
            frame_ret = iolink_master_on_rx(port, port->rx.buf, port->rx.len);
            port->rx.len = 0U;

            if(frame_ret != 0)
            {
                return frame_ret;
            }

            frames++;
        }
    }

    if(recv_ret < 0)
    {
        port->state = IOLINK_MASTER_STATE_ERROR;
        port->rx.len = 0U;
        return -2;
    }

    return frames;
}

int iolink_master_on_rx(iolink_master_port_t* port, const uint8_t* data, uint8_t len)
{
    iolink_frame_operate_response_t resp;

    if((port == NULL) || (data == NULL) || (len == 0U))
    {
        return -1;
    }

    if(port->state == IOLINK_MASTER_STATE_STARTUP)
    {
        if(len != IOLINK_M_SEQ_TYPE0_LEN)
        {
            return -2;
        }

        if(iolink_checksum_ck(data[0], 0U) != data[1])
        {
            port->diagnostics.checksum_errors++;
            if(port->diagnostics.rx_retry_count < 2U)
            {
                port->diagnostics.rx_retry_count++;
            }
            else
            {
                port->state = IOLINK_MASTER_STATE_ERROR;
            }
            return -3;
        }

        port->diagnostics.rx_retry_count = 0U;
        port->state = IOLINK_MASTER_STATE_PREOPERATE;
        return 0;
    }

    if((port->state == IOLINK_MASTER_STATE_OPERATE) &&
       (port->config.m_seq_type == IOLINK_MASTER_M_SEQ_TYPE_0) &&
       (port->config.pd_in_len == 0U) && (port->pd_out_len == 0U))
    {
        if(len != IOLINK_M_SEQ_TYPE0_LEN)
        {
            return -2;
        }

        if(iolink_checksum_ck(data[0], 0U) != data[1])
        {
            port->diagnostics.checksum_errors++;
            if(port->diagnostics.rx_retry_count < 2U)
            {
                port->diagnostics.rx_retry_count++;
            }
            else
            {
                port->state = IOLINK_MASTER_STATE_ERROR;
            }
            return -3;
        }

        port->diagnostics.rx_retry_count = 0U;
        iolink_master_isdu_on_od(port, data, 1U);
        return 0;
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
        port->diagnostics.checksum_errors++;
        if(port->diagnostics.rx_retry_count < 2U)
        {
            port->diagnostics.rx_retry_count++;
        }
        else
        {
            port->state = IOLINK_MASTER_STATE_ERROR;
        }
        return -3;
    }

    port->diagnostics.rx_retry_count = 0U;
    port->diagnostics.od_status = resp.status;
    port->diagnostics.event_pending = resp.event_pending;

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

int iolink_master_get_od_status(const iolink_master_port_t* port, uint8_t* status)
{
    if((port == NULL) || (status == NULL))
    {
        return -1;
    }

    *status = port->diagnostics.od_status;
    return 0;
}

uint8_t iolink_master_get_device_status(const iolink_master_port_t* port)
{
    if(port == NULL)
    {
        return IOLINK_DEVICE_STATUS_FAILURE;
    }

    return (uint8_t)(port->diagnostics.od_status & IOLINK_OD_STATUS_DEVICE_MASK);
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
