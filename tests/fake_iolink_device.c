#include "fake_iolink_device.h"

#include "iolinki/crc.h"
#include "iolinki/frame.h"
#include "iolinki/protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define FAKE_IOLINK_DEVICE_OBJECT_MAX_LEN 16U
#define FAKE_IOLINK_DEVICE_ISDU_REQUEST_MAX_LEN 8U

typedef struct
{
    uint8_t pd_in_value;
    uint8_t pd_in_len;
    uint8_t od_len;
    uint8_t rx_queue[16];
    uint8_t rx_len;
    uint8_t rx_pos;
    uint32_t wakeup_count;
    uint32_t transition_count;
    uint32_t operate_cycle_count;
    uint16_t object_index;
    uint8_t object_subindex;
    uint8_t object_data[FAKE_IOLINK_DEVICE_OBJECT_MAX_LEN];
    uint8_t object_len;
    bool object_valid;
    uint8_t isdu_request[FAKE_IOLINK_DEVICE_ISDU_REQUEST_MAX_LEN];
    uint8_t isdu_request_len;
    bool isdu_request_expect_data;
    bool isdu_request_last_control;
    uint8_t isdu_response[FAKE_IOLINK_DEVICE_OBJECT_MAX_LEN];
    uint8_t isdu_response_len;
    uint8_t isdu_response_pos;
    bool isdu_response_active;
} fake_iolink_device_t;

static fake_iolink_device_t g_device;

static void fake_iolink_device_prepare_isdu_error(uint8_t error)
{
    g_device.isdu_response[0] = 0x80U;
    g_device.isdu_response[1] = error;
    g_device.isdu_response_len = 2U;
    g_device.isdu_response_pos = 0U;
    g_device.isdu_response_active = true;
}

static void fake_iolink_device_prepare_isdu_response(void)
{
    uint8_t service;
    uint16_t index;
    uint8_t subindex;

    if(g_device.isdu_request_len < 4U)
    {
        fake_iolink_device_prepare_isdu_error(IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);
        return;
    }

    service = (uint8_t)(g_device.isdu_request[0] >> 4);
    index = (uint16_t)(((uint16_t)g_device.isdu_request[1] << 8) | g_device.isdu_request[2]);
    subindex = g_device.isdu_request[3];

    if(service != IOLINK_ISDU_SERVICE_READ)
    {
        fake_iolink_device_prepare_isdu_error(IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);
        return;
    }

    if(!g_device.object_valid || (index != g_device.object_index) || (subindex != g_device.object_subindex))
    {
        fake_iolink_device_prepare_isdu_error(IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);
        return;
    }

    memcpy(g_device.isdu_response, g_device.object_data, g_device.object_len);
    g_device.isdu_response_len = g_device.object_len;
    g_device.isdu_response_pos = 0U;
    g_device.isdu_response_active = true;
}

static void fake_iolink_device_on_master_od(uint8_t od)
{
    if(!g_device.isdu_request_expect_data)
    {
        if((g_device.isdu_request_len == 0U) && ((od & IOLINK_ISDU_CTRL_START) == 0U))
        {
            return;
        }

        if((od & IOLINK_ISDU_CTRL_START) != 0U)
        {
            g_device.isdu_request_len = 0U;
        }

        g_device.isdu_request_last_control = ((od & IOLINK_ISDU_CTRL_LAST) != 0U);
        g_device.isdu_request_expect_data = true;
        return;
    }

    if(g_device.isdu_request_len < FAKE_IOLINK_DEVICE_ISDU_REQUEST_MAX_LEN)
    {
        g_device.isdu_request[g_device.isdu_request_len++] = od;
    }

    if(g_device.isdu_request_last_control)
    {
        fake_iolink_device_prepare_isdu_response();
        g_device.isdu_request_len = 0U;
    }

    g_device.isdu_request_expect_data = false;
}

static uint8_t fake_iolink_device_next_response_od(void)
{
    uint8_t data_index;
    uint8_t od;

    if(!g_device.isdu_response_active || (g_device.isdu_response_len == 0U))
    {
        return 0U;
    }

    data_index = (uint8_t)(g_device.isdu_response_pos / 2U);
    if((g_device.isdu_response_pos & 1U) == 0U)
    {
        od = (uint8_t)(data_index & IOLINK_ISDU_CTRL_SEQ_MASK);
        if(data_index == 0U)
        {
            od |= IOLINK_ISDU_CTRL_START;
        }
        if((uint8_t)(data_index + 1U) >= g_device.isdu_response_len)
        {
            od |= IOLINK_ISDU_CTRL_LAST;
        }
    }
    else
    {
        od = g_device.isdu_response[data_index];
    }

    g_device.isdu_response_pos++;
    if(g_device.isdu_response_pos >= (uint8_t)(g_device.isdu_response_len * 2U))
    {
        g_device.isdu_response_active = false;
    }

    return od;
}

static void fake_iolink_device_queue_type0(uint8_t value)
{
    g_device.rx_queue[0] = value;
    g_device.rx_queue[1] = iolink_checksum_ck(value, 0U);
    g_device.rx_len = IOLINK_M_SEQ_TYPE0_LEN;
    g_device.rx_pos = 0U;
}

static void fake_iolink_device_queue_operate_response(void)
{
    uint8_t pos = 0U;
    uint8_t i;

    g_device.rx_queue[pos++] = IOLINK_OD_STATUS_PD_VALID | IOLINK_DEVICE_STATUS_OK;

    for(i = 0U; i < g_device.pd_in_len; i++)
    {
        g_device.rx_queue[pos++] = g_device.pd_in_value;
    }

    for(i = 0U; i < g_device.od_len; i++)
    {
        g_device.rx_queue[pos++] = fake_iolink_device_next_response_od();
    }

    g_device.rx_queue[pos] = iolink_crc6(g_device.rx_queue, pos);
    g_device.rx_len = (uint8_t)(pos + 1U);
    g_device.rx_pos = 0U;
}

static int fake_iolink_device_send(const uint8_t* data, size_t len)
{
    if((data == NULL) || (len == 0U))
    {
        return -1;
    }

    if((len == 1U) && (data[0] == 0x55U))
    {
        g_device.wakeup_count++;
        return (int)len;
    }

    if(len == IOLINK_M_SEQ_TYPE0_LEN)
    {
        if(data[0] == IOLINK_MC_TRANSITION_COMMAND)
        {
            g_device.transition_count++;
            return (int)len;
        }

        fake_iolink_device_on_master_od(data[0]);
        fake_iolink_device_queue_type0(fake_iolink_device_next_response_od());
        return (int)len;
    }

    g_device.operate_cycle_count++;
    if(len > IOLINK_M_SEQ_HEADER_LEN)
    {
        fake_iolink_device_on_master_od(data[IOLINK_M_SEQ_HEADER_LEN]);
    }
    fake_iolink_device_queue_operate_response();
    return (int)len;
}

static int fake_iolink_device_recv_byte(uint8_t* byte)
{
    if(byte == NULL)
    {
        return -1;
    }

    if(g_device.rx_pos >= g_device.rx_len)
    {
        return 0;
    }

    *byte = g_device.rx_queue[g_device.rx_pos++];
    return 1;
}

static const iolink_phy_api_t g_phy = {
    .send = fake_iolink_device_send,
    .recv_byte = fake_iolink_device_recv_byte,
};

void fake_iolink_device_reset(uint8_t pd_in_value, uint8_t pd_in_len, uint8_t od_len)
{
    memset(&g_device, 0, sizeof(g_device));
    g_device.pd_in_value = pd_in_value;
    g_device.pd_in_len = pd_in_len;
    g_device.od_len = od_len;
}

void fake_iolink_device_set_isdu_object(uint16_t index, uint8_t subindex, const uint8_t* data, uint8_t len)
{
    if((data == NULL) || (len == 0U) || (len > FAKE_IOLINK_DEVICE_OBJECT_MAX_LEN))
    {
        return;
    }

    g_device.object_index = index;
    g_device.object_subindex = subindex;
    memcpy(g_device.object_data, data, len);
    g_device.object_len = len;
    g_device.object_valid = true;
}

const iolink_phy_api_t* fake_iolink_device_phy(void)
{
    return &g_phy;
}

uint32_t fake_iolink_device_wakeup_count(void)
{
    return g_device.wakeup_count;
}

uint32_t fake_iolink_device_transition_count(void)
{
    return g_device.transition_count;
}

uint32_t fake_iolink_device_operate_cycle_count(void)
{
    return g_device.operate_cycle_count;
}
