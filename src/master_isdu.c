#include "master_internal.h"

#include "iolinki/protocol.h"

#include <string.h>

static bool iolink_master_isdu_busy(const iolink_master_port_t* port)
{
    return (port->isdu.op != IOLINK_MASTER_ISDU_OP_NONE) && !port->isdu.done;
}

static bool iolink_master_isdu_matches(const iolink_master_port_t* port,
                                       iolink_master_isdu_op_t op,
                                       uint16_t index,
                                       uint8_t subindex)
{
    return (port->isdu.op == op) && (port->isdu.index == index) &&
           (port->isdu.subindex == subindex);
}

static void iolink_master_isdu_clear(iolink_master_port_t* port)
{
    port->isdu.op = IOLINK_MASTER_ISDU_OP_NONE;
    port->isdu.index = 0U;
    port->isdu.subindex = 0U;
    port->isdu.request_len = 0U;
    port->isdu.request_pos = 0U;
    port->isdu.request_seq = 0U;
    port->isdu.request_control_phase = true;
    port->isdu.request_sent = false;
    port->isdu.response_len = 0U;
    port->isdu.response_seq = 0U;
    port->isdu.response_expect_control = true;
    port->isdu.response_last = false;
    port->isdu.done = false;
    port->isdu.error = IOLINK_ISDU_ERROR_NONE;
}

static void iolink_master_isdu_start(iolink_master_port_t* port,
                                     iolink_master_isdu_op_t op,
                                     uint16_t index,
                                     uint8_t subindex)
{
    port->isdu.op = op;
    port->isdu.index = index;
    port->isdu.subindex = subindex;
    port->isdu.request_pos = 0U;
    port->isdu.request_seq = 0U;
    port->isdu.request_control_phase = true;
    port->isdu.request_sent = false;
    port->isdu.response_len = 0U;
    port->isdu.response_seq = 0U;
    port->isdu.response_expect_control = true;
    port->isdu.response_last = false;
    port->isdu.done = false;
    port->isdu.error = IOLINK_ISDU_ERROR_NONE;
}

static int iolink_master_isdu_finish_read(iolink_master_port_t* port,
                                          uint8_t* data,
                                          uint8_t* len)
{
    uint16_t result_len = port->isdu.response_len;

    if(port->isdu.error != IOLINK_ISDU_ERROR_NONE)
    {
        iolink_master_isdu_clear(port);
        return -4;
    }

    if((result_len >= 2U) && (port->isdu.response[0] == 0x80U))
    {
        port->isdu.error = port->isdu.response[1];
        iolink_master_isdu_clear(port);
        return -4;
    }

    if(*len < result_len)
    {
        *len = (result_len > UINT8_MAX) ? UINT8_MAX : (uint8_t)result_len;
        return -2;
    }

    if(result_len > 0U)
    {
        memcpy(data, port->isdu.response, result_len);
    }
    *len = result_len;
    iolink_master_isdu_clear(port);
    return 0;
}

static int iolink_master_isdu_finish_write(iolink_master_port_t* port)
{
    if(port->isdu.error != IOLINK_ISDU_ERROR_NONE)
    {
        iolink_master_isdu_clear(port);
        return -4;
    }

    if((port->isdu.response_len >= 2U) && (port->isdu.response[0] == 0x80U))
    {
        port->isdu.error = port->isdu.response[1];
        iolink_master_isdu_clear(port);
        return -4;
    }

    iolink_master_isdu_clear(port);
    return 0;
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

    if((port->isdu.op == IOLINK_MASTER_ISDU_OP_NONE) || port->isdu.request_sent)
    {
        return;
    }

    for(i = 0U; i < od_len; i++)
    {
        if(port->isdu.request_sent)
        {
            return;
        }

        if(port->isdu.request_control_phase)
        {
            ctrl = (uint8_t)(port->isdu.request_seq & IOLINK_ISDU_CTRL_SEQ_MASK);
            if(port->isdu.request_pos == 0U)
            {
                ctrl |= IOLINK_ISDU_CTRL_START;
            }
            if((uint8_t)(port->isdu.request_pos + 1U) >= port->isdu.request_len)
            {
                ctrl |= IOLINK_ISDU_CTRL_LAST;
            }

            od[i] = ctrl;
            port->isdu.request_control_phase = false;
        }
        else
        {
            od[i] = port->isdu.request[port->isdu.request_pos++];
            if(port->isdu.request_pos >= port->isdu.request_len)
            {
                port->isdu.request_sent = true;
            }
            else
            {
                port->isdu.request_seq =
                    (uint8_t)((port->isdu.request_seq + 1U) & IOLINK_ISDU_CTRL_SEQ_MASK);
                port->isdu.request_control_phase = true;
            }
        }
    }
}

void iolink_master_isdu_on_od(iolink_master_port_t* port, const uint8_t* od, uint8_t od_len)
{
    uint8_t i;
    uint8_t byte;
    uint8_t seq;

    if((port == NULL) || (od == NULL) || (port->isdu.op == IOLINK_MASTER_ISDU_OP_NONE) ||
       port->isdu.done)
    {
        return;
    }

    for(i = 0U; i < od_len; i++)
    {
        byte = od[i];

        if(port->isdu.response_expect_control)
        {
            if((byte & IOLINK_ISDU_CTRL_START) != 0U)
            {
                port->isdu.response_len = 0U;
                port->isdu.response_seq = 0U;
            }

            seq = (uint8_t)(byte & IOLINK_ISDU_CTRL_SEQ_MASK);
            if(seq != port->isdu.response_seq)
            {
                port->isdu.error = IOLINK_ISDU_ERROR_SEGMENTATION;
                port->isdu.done = true;
                return;
            }

            port->isdu.response_last = ((byte & IOLINK_ISDU_CTRL_LAST) != 0U);
            port->isdu.response_expect_control = false;
        }
        else
        {
            if(port->isdu.response_len >= IOLINK_ISDU_BUFFER_SIZE)
            {
                port->isdu.error = IOLINK_ISDU_ERROR_SEGMENTATION;
                port->isdu.done = true;
                return;
            }

            port->isdu.response[port->isdu.response_len++] = byte;

            if(port->isdu.response_last)
            {
                port->isdu.done = true;
                return;
            }

            port->isdu.response_seq =
                (uint8_t)((port->isdu.response_seq + 1U) & IOLINK_ISDU_CTRL_SEQ_MASK);
            port->isdu.response_expect_control = true;
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
        return -1;
    }

    if(port->state != IOLINK_MASTER_STATE_OPERATE)
    {
        return -5;
    }

    if(iolink_master_isdu_busy(port))
    {
        if(iolink_master_isdu_matches(port, IOLINK_MASTER_ISDU_OP_READ, index, subindex))
        {
            return 1;
        }
        return -3;
    }

    if(port->isdu.done)
    {
        if(!iolink_master_isdu_matches(port, IOLINK_MASTER_ISDU_OP_READ, index, subindex))
        {
            return -3;
        }
        return iolink_master_isdu_finish_read(port, data, len);
    }

    iolink_master_isdu_start(port, IOLINK_MASTER_ISDU_OP_READ, index, subindex);
    port->isdu.request[0] = (uint8_t)(IOLINK_ISDU_SERVICE_READ << 4);
    port->isdu.request[1] = (uint8_t)(index >> 8);
    port->isdu.request[2] = (uint8_t)(index & 0xFFU);
    port->isdu.request[3] = subindex;
    port->isdu.request_len = 4U;

    return 1;
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
        return -1;
    }

    if(port->state != IOLINK_MASTER_STATE_OPERATE)
    {
        return -5;
    }

    if(iolink_master_isdu_busy(port))
    {
        if(iolink_master_isdu_matches(port, IOLINK_MASTER_ISDU_OP_WRITE, index, subindex))
        {
            return 1;
        }
        return -3;
    }

    if(port->isdu.done)
    {
        if(!iolink_master_isdu_matches(port, IOLINK_MASTER_ISDU_OP_WRITE, index, subindex))
        {
            return -3;
        }
        return iolink_master_isdu_finish_write(port);
    }

    if(len > (uint8_t)(IOLINK_ISDU_BUFFER_SIZE - 5U))
    {
        return -2;
    }

    iolink_master_isdu_start(port, IOLINK_MASTER_ISDU_OP_WRITE, index, subindex);

    if(len >= 15U)
    {
        port->isdu.request[pos++] = (uint8_t)((IOLINK_ISDU_SERVICE_WRITE << 4) | 0x0FU);
        port->isdu.request[pos++] = len;
    }
    else
    {
        port->isdu.request[pos++] = (uint8_t)((IOLINK_ISDU_SERVICE_WRITE << 4) | len);
    }

    port->isdu.request[pos++] = (uint8_t)(index >> 8);
    port->isdu.request[pos++] = (uint8_t)(index & 0xFFU);
    port->isdu.request[pos++] = subindex;

    if(len > 0U)
    {
        memcpy(&port->isdu.request[pos], data, len);
        pos = (uint8_t)(pos + len);
    }

    port->isdu.request_len = pos;

    return 1;
}
