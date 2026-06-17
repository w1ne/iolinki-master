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
    const iolink_master_port_state_t* state = iolink_master_port_const_state(port);

    if(state->config.auto_baudrate)
    {
        return g_iolink_master_baudrate_scan[state->startup.baudrate_index];
    }

    return state->config.baudrate;
}

static bool iolink_master_send_full(iolink_master_port_t* port, const uint8_t* data, size_t len)
{
    iolink_master_port_state_t* state = iolink_master_port_state(port);
    int sent = state->phy->send(data, len);

    if(sent == (int)len)
    {
        return true;
    }

    state->diagnostics.send_errors++;
    state->state = IOLINK_MASTER_STATE_ERROR;
    return false;
}

static uint8_t iolink_master_decode_pd_descriptor(uint8_t descriptor)
{
    if(descriptor == 0U)
    {
        return 0U;
    }

    if((descriptor & 0x80U) != 0U)
    {
        return (uint8_t)((descriptor & 0x7FU) + 1U);
    }

    return (uint8_t)(descriptor / 8U);
}

static uint8_t iolink_master_mseq_capability_code(iolink_master_m_seq_type_t type)
{
    switch(type)
    {
    case IOLINK_MASTER_M_SEQ_TYPE_1_1:
    case IOLINK_MASTER_M_SEQ_TYPE_1_2:
        return 1U;
    case IOLINK_MASTER_M_SEQ_TYPE_1_V:
    case IOLINK_MASTER_M_SEQ_TYPE_2_V:
        return 5U;
    default:
        return 0U;
    }
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
    iolink_master_port_state(port)->phy = phy;
    iolink_master_port_state(port)->config = *config;
    iolink_master_port_state(port)->od_len = iolink_master_od_len_for_type(config->m_seq_type);
    iolink_master_port_state(port)->pd_in_len = config->pd_in_len;
    iolink_master_port_state(port)->pd_out_len = config->pd_out_len;
    iolink_master_port_state(port)->startup.baudrate_index = 0U;
    iolink_master_port_state(port)->state = (config->port_mode == IOLINK_MASTER_PORT_MODE_IOLINK)
                      ? IOLINK_MASTER_STATE_STARTUP
                      : IOLINK_MASTER_STATE_INACTIVE;

    if(phy->init != NULL)
    {
        ret = phy->init();
        if(ret != 0)
        {
            iolink_master_port_state(port)->state = IOLINK_MASTER_STATE_ERROR;
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

    if((port == NULL) || (iolink_master_port_state(port)->phy == NULL))
    {
        return -1;
    }

    phy = iolink_master_port_state(port)->phy;
    config = iolink_master_port_state(port)->config;

    return iolink_master_init(port, phy, &config);
}

int iolink_master_controller_init(iolink_master_controller_t* controller,
                                  iolink_master_port_t* ports,
                                  uint8_t port_count,
                                  const iolink_phy_api_t* phys,
                                  const iolink_master_config_t* configs)
{
    uint8_t i;
    int ret;

    if((controller == NULL) || (ports == NULL) || (port_count == 0U) || (phys == NULL) ||
       (configs == NULL))
    {
        return -1;
    }

    memset(controller, 0, sizeof(*controller));
    iolink_master_controller_state(controller)->ports = ports;
    iolink_master_controller_state(controller)->port_count = port_count;

    for(i = 0U; i < port_count; i++)
    {
        ret = iolink_master_init(&ports[i], &phys[i], &configs[i]);
        if(ret != 0)
        {
            iolink_master_controller_state(controller)->port_count = i;
            return ret;
        }
    }

    return 0;
}

int iolink_master_controller_tick(iolink_master_controller_t* controller,
                                  const bool* response_timeouts)
{
    uint8_t i;
    int ret;
    int first_error = 0;
    bool timeout;

    if(controller == NULL)
    {
        return -1;
    }

    for(i = 0U; i < iolink_master_controller_state(controller)->port_count; i++)
    {
        timeout = (response_timeouts != NULL) ? response_timeouts[i] : false;
        ret = iolink_master_tick(&iolink_master_controller_state(controller)->ports[i], timeout);
        if((ret < 0) && (first_error == 0))
        {
            first_error = ret;
        }
    }

    return first_error;
}

int iolink_master_on_timeout(iolink_master_port_t* port)
{
    if(port == NULL)
    {
        return -1;
    }

    if(iolink_master_port_state(port)->state != IOLINK_MASTER_STATE_STARTUP)
    {
        if(iolink_master_port_state(port)->state == IOLINK_MASTER_STATE_OPERATE)
        {
            if(iolink_master_port_state(port)->diagnostics.rx_retry_count < 2U)
            {
                iolink_master_port_state(port)->diagnostics.rx_retry_count++;
                return 1;
            }

            iolink_master_port_state(port)->state = IOLINK_MASTER_STATE_ERROR;
            return -2;
        }

        return 0;
    }

    if(iolink_master_port_state(port)->phy == NULL)
    {
        return -1;
    }

    if(iolink_master_port_state(port)->config.auto_baudrate &&
       (iolink_master_port_state(port)->startup.baudrate_index <
        (uint8_t)((sizeof(g_iolink_master_baudrate_scan) /
                   sizeof(g_iolink_master_baudrate_scan[0])) -
                  1U)))
    {
        iolink_master_port_state(port)->startup.baudrate_index++;
        iolink_master_port_state(port)->startup.step = 0U;
        if(iolink_master_port_state(port)->phy->set_baudrate != NULL)
        {
            iolink_master_port_state(port)->phy->set_baudrate(iolink_master_startup_baudrate(port));
        }
        return 1;
    }

    iolink_master_port_state(port)->state = IOLINK_MASTER_STATE_ERROR;
    return -2;
}

int iolink_master_tick(iolink_master_port_t* port, bool response_timeout)
{
    int rx_ret;
    int timeout_ret;

    if(port == NULL)
    {
        return -1;
    }

    rx_ret = iolink_master_poll_rx(port);
    if(rx_ret < 0)
    {
        return rx_ret;
    }

    if(response_timeout)
    {
        timeout_ret = iolink_master_on_timeout(port);
        if(timeout_ret < 0)
        {
            return timeout_ret;
        }
    }

    iolink_master_process(port);
    return rx_ret;
}

void iolink_master_process(iolink_master_port_t* port)
{
    int frame_len;
    size_t od_pos;

    if((port == NULL) || (iolink_master_port_state(port)->phy == NULL) || (iolink_master_port_state(port)->phy->send == NULL))
    {
        return;
    }

    if(iolink_master_port_state(port)->state == IOLINK_MASTER_STATE_STARTUP)
    {
        if(iolink_master_port_state(port)->startup.step == 0U)
        {
            iolink_master_port_state(port)->tx_buf[0] = 0x55U;
            if(iolink_master_send_full(port, iolink_master_port_state(port)->tx_buf, 1U))
            {
                iolink_master_port_state(port)->startup.step++;
            }
            return;
        }

        if(iolink_master_port_state(port)->startup.step == 1U)
        {
            frame_len = iolink_frame_encode_type0(0x00U, iolink_master_port_state(port)->tx_buf, sizeof(iolink_master_port_state(port)->tx_buf));
            if(frame_len > 0)
            {
                if(iolink_master_send_full(port, iolink_master_port_state(port)->tx_buf, (size_t)frame_len))
                {
                    iolink_master_port_state(port)->startup.step++;
                }
            }
            return;
        }
    }

    if(iolink_master_port_state(port)->state == IOLINK_MASTER_STATE_PREOPERATE)
    {
        int ret;

        if((iolink_master_port_state(port)->isdu.op != IOLINK_MASTER_ISDU_OP_NONE) && !iolink_master_port_state(port)->isdu.done)
        {
            uint8_t od = 0U;
            iolink_master_isdu_fill_od(port, &od, 1U);
            frame_len = iolink_frame_encode_type0(od, iolink_master_port_state(port)->tx_buf, sizeof(iolink_master_port_state(port)->tx_buf));
            if(frame_len > 0)
            {
                (void)iolink_master_send_full(port, iolink_master_port_state(port)->tx_buf, (size_t)frame_len);
            }
            return;
        }

        if(iolink_master_port_state(port)->config.validate_device_info && !iolink_master_port_state(port)->device_info.valid)
        {
            ret = iolink_master_read_device_info(port);
            if(ret == 1)
            {
                return;
            }
            if(ret < 0)
            {
                iolink_master_port_state(port)->state = IOLINK_MASTER_STATE_ERROR;
                return;
            }
        }

        if(iolink_master_port_state(port)->config.validate_device_info && (iolink_master_validate_device_info(port) != 0))
        {
            iolink_master_port_state(port)->state = IOLINK_MASTER_STATE_ERROR;
            return;
        }

        frame_len = iolink_frame_encode_type0(IOLINK_MC_TRANSITION_COMMAND,
                                              iolink_master_port_state(port)->tx_buf,
                                              sizeof(iolink_master_port_state(port)->tx_buf));
        if(frame_len > 0)
        {
            if(iolink_master_send_full(port, iolink_master_port_state(port)->tx_buf, (size_t)frame_len))
            {
                iolink_master_port_state(port)->startup.step++;
                iolink_master_port_state(port)->state = IOLINK_MASTER_STATE_OPERATE;
            }
        }
        return;
    }

    if(iolink_master_port_state(port)->state == IOLINK_MASTER_STATE_OPERATE)
    {
        if((iolink_master_port_state(port)->config.m_seq_type == IOLINK_MASTER_M_SEQ_TYPE_0) &&
           (iolink_master_port_state(port)->config.pd_in_len == 0U) && (iolink_master_port_state(port)->pd_out_len == 0U))
        {
            uint8_t od = 0U;
            iolink_master_isdu_fill_od(port, &od, 1U);
            frame_len = iolink_frame_encode_type0(od, iolink_master_port_state(port)->tx_buf, sizeof(iolink_master_port_state(port)->tx_buf));
            if(frame_len > 0)
            {
                if(iolink_master_send_full(port, iolink_master_port_state(port)->tx_buf, (size_t)frame_len))
                {
                    iolink_master_port_state(port)->cycle_count++;
                }
            }
            return;
        }

        frame_len = iolink_frame_encode_type1_cycle(iolink_master_port_state(port)->pd_out,
                                                    iolink_master_port_state(port)->pd_out_len,
                                                    iolink_master_port_state(port)->od_len,
                                                    iolink_master_port_state(port)->tx_buf,
                                                    sizeof(iolink_master_port_state(port)->tx_buf));
        if(frame_len > 0)
        {
            od_pos = (size_t)IOLINK_M_SEQ_HEADER_LEN + iolink_master_port_state(port)->pd_out_len;
            iolink_master_isdu_fill_od(port, &iolink_master_port_state(port)->tx_buf[od_pos], iolink_master_port_state(port)->od_len);
            iolink_master_port_state(port)->tx_buf[frame_len - 1] = iolink_crc6(iolink_master_port_state(port)->tx_buf, (uint8_t)(frame_len - 1));

            if(iolink_master_send_full(port, iolink_master_port_state(port)->tx_buf, (size_t)frame_len))
            {
                iolink_master_port_state(port)->cycle_count++;
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

    if((port == NULL) || (iolink_master_port_state(port)->phy == NULL))
    {
        return -1;
    }

    if(iolink_master_port_state(port)->phy->recv_byte == NULL)
    {
        return 0;
    }

    if((iolink_master_port_state(port)->state == IOLINK_MASTER_STATE_STARTUP) && (iolink_master_port_state(port)->startup.step >= 2U))
    {
        expected_len = IOLINK_M_SEQ_TYPE0_LEN;
    }
    else if(iolink_master_port_state(port)->state == IOLINK_MASTER_STATE_PREOPERATE)
    {
        expected_len = IOLINK_M_SEQ_TYPE0_LEN;
    }
    else if(iolink_master_port_state(port)->state == IOLINK_MASTER_STATE_OPERATE)
    {
        expected_len = (uint8_t)(1U + iolink_master_port_state(port)->config.pd_in_len + iolink_master_port_state(port)->od_len + 1U);
    }
    else
    {
        return 0;
    }

    while((recv_ret = iolink_master_port_state(port)->phy->recv_byte(&byte)) > 0)
    {
        if(iolink_master_port_state(port)->rx.len >= sizeof(iolink_master_port_state(port)->rx.buf))
        {
            iolink_master_port_state(port)->state = IOLINK_MASTER_STATE_ERROR;
            iolink_master_port_state(port)->rx.len = 0U;
            return -3;
        }

        iolink_master_port_state(port)->rx.buf[iolink_master_port_state(port)->rx.len++] = byte;

        if(iolink_master_port_state(port)->rx.len >= expected_len)
        {
            frame_ret = iolink_master_on_rx(port, iolink_master_port_state(port)->rx.buf, iolink_master_port_state(port)->rx.len);
            iolink_master_port_state(port)->rx.len = 0U;

            if(frame_ret != 0)
            {
                return frame_ret;
            }

            frames++;
        }
    }

    if(recv_ret < 0)
    {
        iolink_master_port_state(port)->state = IOLINK_MASTER_STATE_ERROR;
        iolink_master_port_state(port)->rx.len = 0U;
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

    if(iolink_master_port_state(port)->state == IOLINK_MASTER_STATE_STARTUP)
    {
        if(len != IOLINK_M_SEQ_TYPE0_LEN)
        {
            return -2;
        }

        if(iolink_checksum_ck(data[0], 0U) != data[1])
        {
            iolink_master_port_state(port)->diagnostics.checksum_errors++;
            if(iolink_master_port_state(port)->diagnostics.rx_retry_count < 2U)
            {
                iolink_master_port_state(port)->diagnostics.rx_retry_count++;
            }
            else
            {
                iolink_master_port_state(port)->state = IOLINK_MASTER_STATE_ERROR;
            }
            return -3;
        }

        iolink_master_port_state(port)->diagnostics.rx_retry_count = 0U;
        iolink_master_port_state(port)->state = IOLINK_MASTER_STATE_PREOPERATE;
        return 0;
    }

    if(iolink_master_port_state(port)->state == IOLINK_MASTER_STATE_PREOPERATE)
    {
        if(len != IOLINK_M_SEQ_TYPE0_LEN)
        {
            return -2;
        }

        if(iolink_checksum_ck(data[0], 0U) != data[1])
        {
            iolink_master_port_state(port)->diagnostics.checksum_errors++;
            if(iolink_master_port_state(port)->diagnostics.rx_retry_count < 2U)
            {
                iolink_master_port_state(port)->diagnostics.rx_retry_count++;
            }
            else
            {
                iolink_master_port_state(port)->state = IOLINK_MASTER_STATE_ERROR;
            }
            return -3;
        }

        iolink_master_port_state(port)->diagnostics.rx_retry_count = 0U;
        iolink_master_isdu_on_od(port, data, 1U);
        return 0;
    }

    if((iolink_master_port_state(port)->state == IOLINK_MASTER_STATE_OPERATE) &&
       (iolink_master_port_state(port)->config.m_seq_type == IOLINK_MASTER_M_SEQ_TYPE_0) &&
       (iolink_master_port_state(port)->config.pd_in_len == 0U) && (iolink_master_port_state(port)->pd_out_len == 0U))
    {
        if(len != IOLINK_M_SEQ_TYPE0_LEN)
        {
            return -2;
        }

        if(iolink_checksum_ck(data[0], 0U) != data[1])
        {
            iolink_master_port_state(port)->diagnostics.checksum_errors++;
            if(iolink_master_port_state(port)->diagnostics.rx_retry_count < 2U)
            {
                iolink_master_port_state(port)->diagnostics.rx_retry_count++;
            }
            else
            {
                iolink_master_port_state(port)->state = IOLINK_MASTER_STATE_ERROR;
            }
            return -3;
        }

        iolink_master_port_state(port)->diagnostics.rx_retry_count = 0U;
        iolink_master_isdu_on_od(port, data, 1U);
        return 0;
    }

    if(iolink_frame_decode_operate_response(data,
                                            len,
                                            iolink_master_port_state(port)->config.pd_in_len,
                                            iolink_master_port_state(port)->od_len,
                                            &resp) != 0)
    {
        return -2;
    }

    if(!resp.checksum_ok)
    {
        iolink_master_port_state(port)->diagnostics.checksum_errors++;
        if(iolink_master_port_state(port)->diagnostics.rx_retry_count < 2U)
        {
            iolink_master_port_state(port)->diagnostics.rx_retry_count++;
        }
        else
        {
            iolink_master_port_state(port)->state = IOLINK_MASTER_STATE_ERROR;
        }
        return -3;
    }

    iolink_master_port_state(port)->diagnostics.rx_retry_count = 0U;
    iolink_master_port_state(port)->diagnostics.od_status = resp.status;
    iolink_master_port_state(port)->diagnostics.event_pending = resp.event_pending;

    if(resp.pd_valid)
    {
        memcpy(iolink_master_port_state(port)->pd_in, resp.pd, resp.pd_len);
        iolink_master_port_state(port)->pd_in_len = resp.pd_len;
        iolink_master_port_state(port)->pd_valid = true;
    }

    iolink_master_isdu_on_od(port, resp.od, resp.od_len);

    return 0;
}

iolink_master_state_t iolink_master_get_state(const iolink_master_port_t* port)
{
    const iolink_master_port_state_t* state;

    if(port == NULL)
    {
        return IOLINK_MASTER_STATE_ERROR;
    }

    state = iolink_master_port_const_state(port);
    return state->state;
}

int iolink_master_get_pd_in(const iolink_master_port_t* port,
                            uint8_t* buffer,
                            uint8_t buffer_len,
                            uint8_t* out_len)
{
    const iolink_master_port_state_t* state;

    if((port == NULL) || (buffer == NULL) || (out_len == NULL))
    {
        return -1;
    }

    state = iolink_master_port_const_state(port);

    if(buffer_len < state->pd_in_len)
    {
        *out_len = state->pd_in_len;
        return -2;
    }

    *out_len = state->pd_in_len;

    if(!state->pd_valid)
    {
        return 1;
    }

    memcpy(buffer, state->pd_in, state->pd_in_len);
    return 0;
}

int iolink_master_get_od_status(const iolink_master_port_t* port, uint8_t* status)
{
    const iolink_master_port_state_t* state;

    if((port == NULL) || (status == NULL))
    {
        return -1;
    }

    state = iolink_master_port_const_state(port);
    *status = state->diagnostics.od_status;
    return 0;
}

uint8_t iolink_master_get_device_status(const iolink_master_port_t* port)
{
    const iolink_master_port_state_t* state;

    if(port == NULL)
    {
        return IOLINK_DEVICE_STATUS_FAILURE;
    }

    state = iolink_master_port_const_state(port);
    return (uint8_t)(state->diagnostics.od_status & IOLINK_OD_STATUS_DEVICE_MASK);
}

int iolink_master_get_diagnostics(const iolink_master_port_t* port,
                                  iolink_master_diagnostics_t* diagnostics)
{
    const iolink_master_port_state_t* state;

    if((port == NULL) || (diagnostics == NULL))
    {
        return -1;
    }

    state = iolink_master_port_const_state(port);
    *diagnostics = state->diagnostics;
    return 0;
}

int iolink_master_set_dq(iolink_master_port_t* port, bool level)
{
    if(port == NULL)
    {
        return -1;
    }

    if(iolink_master_port_state(port)->config.port_mode != IOLINK_MASTER_PORT_MODE_DQ)
    {
        return -2;
    }

    if((iolink_master_port_state(port)->phy == NULL) || (iolink_master_port_state(port)->phy->set_cq_line == NULL))
    {
        return -3;
    }

    iolink_master_port_state(port)->phy->set_cq_line(level ? 1U : 0U);
    return 0;
}

int iolink_master_parse_direct_parameter_page1(const uint8_t* page,
                                               uint8_t len,
                                               iolink_master_device_info_t* info)
{
    if((page == NULL) || (info == NULL))
    {
        return -1;
    }

    if(len < 16U)
    {
        return -2;
    }

    memset(info, 0, sizeof(*info));
    info->valid = true;
    info->min_cycle_time = page[0x02];
    info->mseq_capability = page[0x03];
    info->isdu_supported = ((page[0x03] & 0x01U) != 0U);
    info->operate_mseq_code = (uint8_t)((page[0x03] >> 1U) & 0x07U);
    info->preoperate_mseq_code = (uint8_t)((page[0x03] >> 4U) & 0x03U);
    info->revision_id = page[0x04];
    info->pd_in_descriptor = page[0x05];
    info->pd_out_descriptor = page[0x06];
    info->pd_in_len = iolink_master_decode_pd_descriptor(page[0x05]);
    info->pd_out_len = iolink_master_decode_pd_descriptor(page[0x06]);
    info->vendor_id = (uint16_t)(((uint16_t)page[0x07] << 8U) | page[0x08]);
    info->device_id = ((uint32_t)page[0x09] << 16U) | ((uint32_t)page[0x0A] << 8U) |
                      (uint32_t)page[0x0B];
    return 0;
}

int iolink_master_apply_direct_parameter_page1(iolink_master_port_t* port,
                                               const uint8_t* page,
                                               uint8_t len)
{
    if(port == NULL)
    {
        return -1;
    }

    return iolink_master_parse_direct_parameter_page1(page, len, &iolink_master_port_state(port)->device_info);
}

int iolink_master_get_device_info(const iolink_master_port_t* port,
                                  iolink_master_device_info_t* info)
{
    const iolink_master_port_state_t* state;

    if((port == NULL) || (info == NULL))
    {
        return -1;
    }

    state = iolink_master_port_const_state(port);
    *info = state->device_info;
    if(!info->valid)
    {
        return 1;
    }

    return 0;
}

int iolink_master_validate_device_info(const iolink_master_port_t* port)
{
    const iolink_master_port_state_t* state;
    const iolink_master_device_info_t* info;

    if(port == NULL)
    {
        return -1;
    }

    state = iolink_master_port_const_state(port);
    info = &state->device_info;
    if(!info->valid)
    {
        return 1;
    }

    if((info->revision_id != 0x10U) && (info->revision_id != 0x11U))
    {
        return -2;
    }

    if(state->config.min_cycle_time < info->min_cycle_time)
    {
        return -3;
    }

    if((state->config.pd_in_len != info->pd_in_len) ||
       (state->config.pd_out_len != info->pd_out_len))
    {
        return -4;
    }

    if(iolink_master_mseq_capability_code(state->config.m_seq_type) != info->operate_mseq_code)
    {
        return -5;
    }

    return 0;
}

int iolink_master_set_pd_out(iolink_master_port_t* port, const uint8_t* data, uint8_t len)
{
    if((port == NULL) || ((data == NULL) && (len > 0U)))
    {
        return -1;
    }

    if((len > IOLINK_PD_OUT_MAX_SIZE) || (len != iolink_master_port_state(port)->config.pd_out_len))
    {
        return -2;
    }

    if(len > 0U)
    {
        memcpy(iolink_master_port_state(port)->pd_out, data, len);
    }
    iolink_master_port_state(port)->pd_out_len = len;
    return 0;
}
