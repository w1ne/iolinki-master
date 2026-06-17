#include "fake_iolink_device.h"

#include "iolinki/crc.h"
#include "iolinki/frame.h"
#include "iolinki/protocol.h"

#include <stddef.h>
#include <string.h>

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
} fake_iolink_device_t;

static fake_iolink_device_t g_device;

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
        g_device.rx_queue[pos++] = 0U;
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

        fake_iolink_device_queue_type0(0x00U);
        return (int)len;
    }

    g_device.operate_cycle_count++;
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
