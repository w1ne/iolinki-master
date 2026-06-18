#include "master_internal.h"

#include "iolinki/protocol.h"

#include <string.h>

static bool iolink_master_isdu_busy(const iolink_master_port_t* port)
{
    const iolink_master_port_state_t* state = iolink_master_port_const_state(port);

    return (state->isdu.op != IOLINK_MASTER_ISDU_OP_NONE) && !state->isdu.done;
}

static bool iolink_master_isdu_matches(const iolink_master_port_t* port,
                                       iolink_master_isdu_op_t op,
                                       uint16_t index,
                                       uint8_t subindex)
{
    const iolink_master_port_state_t* state = iolink_master_port_const_state(port);

    return (state->isdu.op == op) && (state->isdu.index == index) &&
           (state->isdu.subindex == subindex);
}

static void iolink_master_isdu_clear(iolink_master_port_t* port)
{
    iolink_master_port_state(port)->isdu.op = IOLINK_MASTER_ISDU_OP_NONE;
    iolink_master_port_state(port)->isdu.index = 0U;
    iolink_master_port_state(port)->isdu.subindex = 0U;
    iolink_master_port_state(port)->isdu.request_len = 0U;
    iolink_master_port_state(port)->isdu.request_pos = 0U;
    iolink_master_port_state(port)->isdu.request_seq = 0U;
    iolink_master_port_state(port)->isdu.request_control_phase = true;
    iolink_master_port_state(port)->isdu.request_sent = false;
    iolink_master_port_state(port)->isdu.response_len = 0U;
    iolink_master_port_state(port)->isdu.response_seq = 0U;
    iolink_master_port_state(port)->isdu.response_expect_control = true;
    iolink_master_port_state(port)->isdu.response_last = false;
    iolink_master_port_state(port)->isdu.done = false;
    iolink_master_port_state(port)->isdu.error = IOLINK_ISDU_ERROR_NONE;
}

static void iolink_master_isdu_start(iolink_master_port_t* port,
                                     iolink_master_isdu_op_t op,
                                     uint16_t index,
                                     uint8_t subindex)
{
    iolink_master_port_state(port)->isdu.op = op;
    iolink_master_port_state(port)->isdu.index = index;
    iolink_master_port_state(port)->isdu.subindex = subindex;
    iolink_master_port_state(port)->isdu.request_pos = 0U;
    iolink_master_port_state(port)->isdu.request_seq = 0U;
    iolink_master_port_state(port)->isdu.request_control_phase = true;
    iolink_master_port_state(port)->isdu.request_sent = false;
    iolink_master_port_state(port)->isdu.response_len = 0U;
    iolink_master_port_state(port)->isdu.response_seq = 0U;
    iolink_master_port_state(port)->isdu.response_expect_control = true;
    iolink_master_port_state(port)->isdu.response_last = false;
    iolink_master_port_state(port)->isdu.done = false;
    iolink_master_port_state(port)->isdu.error = IOLINK_ISDU_ERROR_NONE;
}

static int iolink_master_isdu_finish_read(iolink_master_port_t* port,
                                          uint8_t* data,
                                          uint8_t* len)
{
    uint16_t result_len = iolink_master_port_state(port)->isdu.response_len;

    if(iolink_master_port_state(port)->isdu.error != IOLINK_ISDU_ERROR_NONE)
    {
        iolink_master_port_state(port)->diagnostics.last_isdu_error =
            iolink_master_port_state(port)->isdu.error;
        iolink_master_isdu_clear(port);
        return IOLINK_MASTER_ISDU_ERR_DEVICE;
    }

    if((result_len >= 2U) && (iolink_master_port_state(port)->isdu.response[0] == 0x80U))
    {
        iolink_master_port_state(port)->isdu.error = iolink_master_port_state(port)->isdu.response[1];
        iolink_master_port_state(port)->diagnostics.last_isdu_error =
            iolink_master_port_state(port)->isdu.error;
        iolink_master_isdu_clear(port);
        return IOLINK_MASTER_ISDU_ERR_DEVICE;
    }

    if(*len < result_len)
    {
        *len = (result_len > UINT8_MAX) ? UINT8_MAX : (uint8_t)result_len;
        return IOLINK_MASTER_ISDU_ERR_BUFFER_TOO_SMALL;
    }

    if(result_len > 0U)
    {
        memcpy(data, iolink_master_port_state(port)->isdu.response, result_len);
    }
    *len = result_len;
    iolink_master_isdu_clear(port);
    return IOLINK_MASTER_STATUS_OK;
}

static int iolink_master_isdu_finish_write(iolink_master_port_t* port)
{
    if(iolink_master_port_state(port)->isdu.error != IOLINK_ISDU_ERROR_NONE)
    {
        iolink_master_port_state(port)->diagnostics.last_isdu_error =
            iolink_master_port_state(port)->isdu.error;
        iolink_master_isdu_clear(port);
        return IOLINK_MASTER_ISDU_ERR_DEVICE;
    }

    if((iolink_master_port_state(port)->isdu.response_len >= 2U) && (iolink_master_port_state(port)->isdu.response[0] == 0x80U))
    {
        iolink_master_port_state(port)->isdu.error = iolink_master_port_state(port)->isdu.response[1];
        iolink_master_port_state(port)->diagnostics.last_isdu_error =
            iolink_master_port_state(port)->isdu.error;
        iolink_master_isdu_clear(port);
        return IOLINK_MASTER_ISDU_ERR_DEVICE;
    }

    iolink_master_isdu_clear(port);
    return IOLINK_MASTER_STATUS_OK;
}

void iolink_master_isdu_fill_od(iolink_master_port_t* port, uint8_t* od, uint8_t od_len)
{
    uint8_t i;
    uint8_t ctrl;

    if((port == NULL) || (od == NULL))
    {
        return;
    }

    memset(od, 0, od_len);

    if((iolink_master_port_state(port)->isdu.op == IOLINK_MASTER_ISDU_OP_NONE) || iolink_master_port_state(port)->isdu.request_sent)
    {
        return;
    }

    for(i = 0U; i < od_len; i++)
    {
        if(iolink_master_port_state(port)->isdu.request_sent)
        {
            return;
        }

        if(iolink_master_port_state(port)->isdu.request_control_phase)
        {
            ctrl = (uint8_t)(iolink_master_port_state(port)->isdu.request_seq & IOLINK_ISDU_CTRL_SEQ_MASK);
            if(iolink_master_port_state(port)->isdu.request_pos == 0U)
            {
                ctrl |= IOLINK_ISDU_CTRL_START;
            }
            if((uint8_t)(iolink_master_port_state(port)->isdu.request_pos + 1U) >= iolink_master_port_state(port)->isdu.request_len)
            {
                ctrl |= IOLINK_ISDU_CTRL_LAST;
            }

            od[i] = ctrl;
            iolink_master_port_state(port)->isdu.request_control_phase = false;
        }
        else
        {
            od[i] = iolink_master_port_state(port)->isdu.request[iolink_master_port_state(port)->isdu.request_pos++];
            if(iolink_master_port_state(port)->isdu.request_pos >= iolink_master_port_state(port)->isdu.request_len)
            {
                iolink_master_port_state(port)->isdu.request_sent = true;
            }
            else
            {
                iolink_master_port_state(port)->isdu.request_seq =
                    (uint8_t)((iolink_master_port_state(port)->isdu.request_seq + 1U) & IOLINK_ISDU_CTRL_SEQ_MASK);
                iolink_master_port_state(port)->isdu.request_control_phase = true;
            }
        }
    }
}

void iolink_master_isdu_on_od(iolink_master_port_t* port, const uint8_t* od, uint8_t od_len)
{
    uint8_t i;
    uint8_t byte;
    uint8_t seq;

    if((port == NULL) || (od == NULL) || (iolink_master_port_state(port)->isdu.op == IOLINK_MASTER_ISDU_OP_NONE) ||
       iolink_master_port_state(port)->isdu.done)
    {
        return;
    }

    for(i = 0U; i < od_len; i++)
    {
        byte = od[i];

        if(!iolink_master_port_state(port)->isdu.request_sent &&
           iolink_master_port_state(port)->isdu.response_expect_control &&
           (iolink_master_port_state(port)->isdu.response_len == 0U) && ((byte & IOLINK_ISDU_CTRL_START) == 0U))
        {
            continue;
        }

        if(iolink_master_port_state(port)->isdu.response_expect_control)
        {
            if((byte & IOLINK_ISDU_CTRL_START) != 0U)
            {
                iolink_master_port_state(port)->isdu.response_len = 0U;
                iolink_master_port_state(port)->isdu.response_seq = 0U;
            }

            seq = (uint8_t)(byte & IOLINK_ISDU_CTRL_SEQ_MASK);
            if(seq != iolink_master_port_state(port)->isdu.response_seq)
            {
                iolink_master_port_state(port)->isdu.error = IOLINK_ISDU_ERROR_SEGMENTATION;
                iolink_master_port_state(port)->isdu.done = true;
                return;
            }

            iolink_master_port_state(port)->isdu.response_last = ((byte & IOLINK_ISDU_CTRL_LAST) != 0U);
            iolink_master_port_state(port)->isdu.response_expect_control = false;
        }
        else
        {
            if(iolink_master_port_state(port)->isdu.response_len >= IOLINK_ISDU_BUFFER_SIZE)
            {
                iolink_master_port_state(port)->isdu.error = IOLINK_ISDU_ERROR_SEGMENTATION;
                iolink_master_port_state(port)->isdu.done = true;
                return;
            }

            iolink_master_port_state(port)->isdu.response[iolink_master_port_state(port)->isdu.response_len++] = byte;

            if(iolink_master_port_state(port)->isdu.response_last)
            {
                iolink_master_port_state(port)->isdu.done = true;
                return;
            }

            iolink_master_port_state(port)->isdu.response_seq =
                (uint8_t)((iolink_master_port_state(port)->isdu.response_seq + 1U) & IOLINK_ISDU_CTRL_SEQ_MASK);
            iolink_master_port_state(port)->isdu.response_expect_control = true;
        }
    }
}

int iolink_master_read_isdu(iolink_master_port_t* port,
                            uint16_t index,
                            uint8_t subindex,
                            uint8_t* data,
                            uint8_t* len)
{
    if((port == NULL) || (data == NULL) || (len == NULL))
    {
        return IOLINK_MASTER_ERR_INVALID_ARG;
    }

    if((iolink_master_port_state(port)->state != IOLINK_MASTER_STATE_OPERATE) &&
       (iolink_master_port_state(port)->state != IOLINK_MASTER_STATE_PREOPERATE))
    {
        return IOLINK_MASTER_ISDU_ERR_INVALID_STATE;
    }

    if(iolink_master_isdu_busy(port))
    {
        if(iolink_master_isdu_matches(port, IOLINK_MASTER_ISDU_OP_READ, index, subindex))
        {
            return IOLINK_MASTER_STATUS_PENDING;
        }
        return IOLINK_MASTER_ISDU_ERR_BUSY;
    }

    if(iolink_master_port_state(port)->isdu.done)
    {
        if(!iolink_master_isdu_matches(port, IOLINK_MASTER_ISDU_OP_READ, index, subindex))
        {
            return IOLINK_MASTER_ISDU_ERR_BUSY;
        }
        return iolink_master_isdu_finish_read(port, data, len);
    }

    iolink_master_isdu_start(port, IOLINK_MASTER_ISDU_OP_READ, index, subindex);
    iolink_master_port_state(port)->isdu.request[0] = (uint8_t)(IOLINK_ISDU_SERVICE_READ << 4);
    iolink_master_port_state(port)->isdu.request[1] = (uint8_t)(index >> 8);
    iolink_master_port_state(port)->isdu.request[2] = (uint8_t)(index & 0xFFU);
    iolink_master_port_state(port)->isdu.request[3] = subindex;
    iolink_master_port_state(port)->isdu.request_len = 4U;

    return IOLINK_MASTER_STATUS_PENDING;
}

int iolink_master_read_device_info(iolink_master_port_t* port)
{
    uint8_t page[16];
    uint8_t len = sizeof(page);
    int ret;

    if(port == NULL)
    {
        return IOLINK_MASTER_ERR_INVALID_ARG;
    }

    ret = iolink_master_read_isdu(port, IOLINK_IDX_DIRECT_PARAMETERS_1, 0U, page, &len);
    if(ret != 0)
    {
        return ret;
    }

    ret = iolink_master_apply_direct_parameter_page1(port, page, len);
    if(ret != 0)
    {
        return ret;
    }

    return iolink_master_validate_device_info(port);
}

int iolink_master_write_isdu(iolink_master_port_t* port,
                             uint16_t index,
                             uint8_t subindex,
                             const uint8_t* data,
                             uint8_t len)
{
    uint8_t pos = 0U;

    if((port == NULL) || ((data == NULL) && (len > 0U)))
    {
        return IOLINK_MASTER_ERR_INVALID_ARG;
    }

    if((iolink_master_port_state(port)->state != IOLINK_MASTER_STATE_OPERATE) &&
       (iolink_master_port_state(port)->state != IOLINK_MASTER_STATE_PREOPERATE))
    {
        return IOLINK_MASTER_ISDU_ERR_INVALID_STATE;
    }

    if(iolink_master_isdu_busy(port))
    {
        if(iolink_master_isdu_matches(port, IOLINK_MASTER_ISDU_OP_WRITE, index, subindex))
        {
            return IOLINK_MASTER_STATUS_PENDING;
        }
        return IOLINK_MASTER_ISDU_ERR_BUSY;
    }

    if(iolink_master_port_state(port)->isdu.done)
    {
        if(!iolink_master_isdu_matches(port, IOLINK_MASTER_ISDU_OP_WRITE, index, subindex))
        {
            return IOLINK_MASTER_ISDU_ERR_BUSY;
        }
        return iolink_master_isdu_finish_write(port);
    }

    if(len > (uint8_t)(IOLINK_ISDU_BUFFER_SIZE - 5U))
    {
        return IOLINK_MASTER_ISDU_ERR_BUFFER_TOO_SMALL;
    }

    iolink_master_isdu_start(port, IOLINK_MASTER_ISDU_OP_WRITE, index, subindex);

    if(len >= 15U)
    {
        iolink_master_port_state(port)->isdu.request[pos++] = (uint8_t)((IOLINK_ISDU_SERVICE_WRITE << 4) | 0x0FU);
        iolink_master_port_state(port)->isdu.request[pos++] = len;
    }
    else
    {
        iolink_master_port_state(port)->isdu.request[pos++] = (uint8_t)((IOLINK_ISDU_SERVICE_WRITE << 4) | len);
    }

    iolink_master_port_state(port)->isdu.request[pos++] = (uint8_t)(index >> 8);
    iolink_master_port_state(port)->isdu.request[pos++] = (uint8_t)(index & 0xFFU);
    iolink_master_port_state(port)->isdu.request[pos++] = subindex;

    if(len > 0U)
    {
        memcpy(&iolink_master_port_state(port)->isdu.request[pos], data, len);
        pos = (uint8_t)(pos + len);
    }

    iolink_master_port_state(port)->isdu.request_len = pos;

    return IOLINK_MASTER_STATUS_PENDING;
}

int iolink_master_read_data_storage(iolink_master_port_t* port, uint8_t* data, uint8_t* len)
{
    return iolink_master_read_isdu(port, IOLINK_IDX_DATA_STORAGE, 0U, data, len);
}

int iolink_master_write_data_storage(iolink_master_port_t* port,
                                     const uint8_t* data,
                                     uint8_t len)
{
    return iolink_master_write_isdu(port, IOLINK_IDX_DATA_STORAGE, 0U, data, len);
}

int iolink_master_read_detailed_device_status(iolink_master_port_t* port,
                                              uint8_t* data,
                                              uint8_t* len)
{
    return iolink_master_read_isdu(port, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0U, data, len);
}

int iolink_master_read_event_code(iolink_master_port_t* port, uint16_t* event_code)
{
    uint8_t data[2] = {0U};
    uint8_t len = sizeof(data);
    int ret;

    if(event_code == NULL)
    {
        return IOLINK_MASTER_ERR_INVALID_ARG;
    }

    ret = iolink_master_read_isdu(port, IOLINK_IDX_SYSTEM_COMMAND, 0U, data, &len);
    if(ret != IOLINK_MASTER_STATUS_OK)
    {
        return ret;
    }

    if(len < sizeof(data))
    {
        return IOLINK_MASTER_ISDU_ERR_DEVICE;
    }

    *event_code = (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
    return IOLINK_MASTER_STATUS_OK;
}

static iolink_master_event_type_t iolink_master_event_type_from_qualifier(uint8_t qualifier)
{
    switch((uint8_t)((qualifier >> 4U) & 0x03U))
    {
    case 1U:
        return IOLINK_MASTER_EVENT_TYPE_NOTIFICATION;
    case 2U:
        return IOLINK_MASTER_EVENT_TYPE_WARNING;
    case 3U:
        return IOLINK_MASTER_EVENT_TYPE_ERROR;
    default:
        return IOLINK_MASTER_EVENT_TYPE_UNKNOWN;
    }
}

int iolink_master_read_event_details(iolink_master_port_t* port,
                                     iolink_master_event_t* events,
                                     uint8_t max_events,
                                     uint8_t* out_count)
{
    uint8_t data[24] = {0U};
    uint8_t len = sizeof(data);
    uint8_t count;
    uint8_t i;
    int ret;

    if((events == NULL) || (out_count == NULL))
    {
        return IOLINK_MASTER_ERR_INVALID_ARG;
    }

    ret = iolink_master_read_detailed_device_status(port, data, &len);
    if(ret != IOLINK_MASTER_STATUS_OK)
    {
        return ret;
    }

    if((len % 3U) != 0U)
    {
        return IOLINK_MASTER_ISDU_ERR_DEVICE;
    }

    count = (uint8_t)(len / 3U);
    *out_count = count;
    if(max_events < count)
    {
        return IOLINK_MASTER_ERR_BUFFER_TOO_SMALL;
    }

    for(i = 0U; i < count; i++)
    {
        events[i].qualifier = data[i * 3U];
        events[i].type = iolink_master_event_type_from_qualifier(events[i].qualifier);
        events[i].code = (uint16_t)(((uint16_t)data[(i * 3U) + 1U] << 8U) |
                                    data[(i * 3U) + 2U]);
    }

    return IOLINK_MASTER_STATUS_OK;
}

static int iolink_master_write_system_command(iolink_master_port_t* port, uint8_t command)
{
    return iolink_master_write_isdu(port, IOLINK_IDX_SYSTEM_COMMAND, 0U, &command, 1U);
}

int iolink_master_begin_parameter_download(iolink_master_port_t* port)
{
    return iolink_master_write_system_command(port, IOLINK_CMD_PARAM_DOWNLOAD_START);
}

int iolink_master_end_parameter_download(iolink_master_port_t* port)
{
    return iolink_master_write_system_command(port, IOLINK_CMD_PARAM_DOWNLOAD_END);
}
