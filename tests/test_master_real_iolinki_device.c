#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "iolinki/application.h"
#include "iolinki/iolink.h"
#include "iolinki_master/master.h"

#define LINK_QUEUE_CAP 64U

typedef struct
{
    uint8_t bytes[LINK_QUEUE_CAP];
    uint8_t head;
    uint8_t len;
} link_queue_t;

static link_queue_t g_master_to_device;
static link_queue_t g_device_to_master;
static int g_wakeup_pending;
static uint8_t g_last_pd_input[4];
static uint8_t g_last_pd_input_len;

static void q_reset(link_queue_t* q)
{
    memset(q, 0, sizeof(*q));
}

static int q_push(link_queue_t* q, const uint8_t* data, size_t len)
{
    size_t i;

    if((data == NULL) || ((size_t)q->len + len > LINK_QUEUE_CAP))
    {
        return -1;
    }

    for(i = 0U; i < len; i++)
    {
        uint8_t tail = (uint8_t)((q->head + q->len) % LINK_QUEUE_CAP);
        q->bytes[tail] = data[i];
        q->len++;
    }

    return (int)len;
}

static int q_pop(link_queue_t* q, uint8_t* byte)
{
    if((byte == NULL) || (q->len == 0U))
    {
        return 0;
    }

    *byte = q->bytes[q->head];
    q->head = (uint8_t)((q->head + 1U) % LINK_QUEUE_CAP);
    q->len--;
    return 1;
}

static int master_send(const uint8_t* data, size_t len)
{
    return q_push(&g_master_to_device, data, len);
}

static int master_recv(uint8_t* byte)
{
    return q_pop(&g_device_to_master, byte);
}

static int master_wake_up(void)
{
    g_wakeup_pending = 1;
    return 0;
}

static int checked_set_mode(iolink_phy_mode_t mode)
{
    (void)mode;
    return 0;
}

static int phy_noop(void)
{
    return 0;
}

static int device_send(const uint8_t* data, size_t len)
{
    return q_push(&g_device_to_master, data, len);
}

static int device_recv(uint8_t* byte)
{
    return q_pop(&g_master_to_device, byte);
}

static int device_detect_wakeup(void)
{
    int ret = g_wakeup_pending;
    g_wakeup_pending = 0;
    return ret;
}

static void device_set_mode(iolink_phy_mode_t mode)
{
    (void)mode;
}

static void device_set_baudrate(iolink_baudrate_t baudrate)
{
    (void)baudrate;
}

static void on_device_pd_input(const uint8_t* data, uint8_t len)
{
    assert_true(len <= sizeof(g_last_pd_input));
    memcpy(g_last_pd_input, data, len);
    g_last_pd_input_len = len;
}

static void pump_device(uint8_t pd_value)
{
    uint8_t pd[1] = {pd_value};
    uint8_t i;

    assert_int_equal(iolink_pd_input_update(pd, sizeof(pd), true), 0);
    for(i = 0U; i < 4U; i++)
    {
        iolink_process();
    }
}

static void test_master_reaches_operate_with_real_iolinki_device_stack(void** state)
{
    static const iolink_phy_api_t master_phy = {
        .send = master_send,
        .recv_byte = master_recv,
    };
    static const iolink_phy_api_t device_phy = {
        .init = phy_noop,
        .set_mode = device_set_mode,
        .set_baudrate = device_set_baudrate,
        .send = device_send,
        .recv_byte = device_recv,
        .detect_wakeup = device_detect_wakeup,
    };
    static const iolink_app_callbacks_t app_callbacks = {
        .on_pd_input = on_device_pd_input,
    };
    iolink_master_port_t master;
    iolink_master_config_t master_config = {
        .port_mode = IOLINK_MASTER_PORT_MODE_IOLINK,
        .m_seq_type = IOLINK_MASTER_M_SEQ_TYPE_1_1,
        .baudrate = IOLINK_BAUDRATE_COM2,
        .min_cycle_time = 1U,
        .pd_in_len = 1U,
        .pd_out_len = 0U,
        .response_timeout_100us = 20U,
        .set_mode_checked = checked_set_mode,
        .prepare_tx = phy_noop,
        .prepare_rx = phy_noop,
        .wake_up = master_wake_up,
    };
    iolink_config_t device_config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_1,
        .min_cycle_time = 1U,
        .pd_in_len = 1U,
        .pd_out_len = 0U,
        .t_pd_us = 0U,
    };
    uint8_t pd_in[1] = {0U};
    uint8_t pd_len = 0U;
    uint8_t i;

    (void)state;

    q_reset(&g_master_to_device);
    q_reset(&g_device_to_master);
    g_wakeup_pending = 0;
    g_last_pd_input_len = 0U;
    memset(g_last_pd_input, 0, sizeof(g_last_pd_input));

    iolink_app_register(&app_callbacks);
    assert_int_equal(iolink_init(&device_phy, &device_config), 0);
    iolink_set_timing_enforcement(false);
    assert_int_equal(iolink_master_init(&master, &master_phy, &master_config), 0);

    for(i = 0U; i < 20U; i++)
    {
        assert_int_equal(iolink_master_tick_at(&master, IOLINK_MASTER_TICK_CYCLE_DUE, i), 0);
        pump_device(0xA5U);
        (void)iolink_master_tick_at(&master, IOLINK_MASTER_TICK_NONE, i + 1U);

        if(iolink_master_get_state(&master) == IOLINK_MASTER_STATE_OPERATE)
        {
            assert_int_equal(iolink_master_tick_at(&master, IOLINK_MASTER_TICK_CYCLE_DUE, i + 40U), 0);
            pump_device(0xA5U);
            assert_int_equal(iolink_master_tick_at(&master, IOLINK_MASTER_TICK_NONE, i + 41U), 1);
            assert_int_equal(iolink_master_get_pd_in(&master, pd_in, sizeof(pd_in), &pd_len), 0);
            assert_int_equal(pd_len, 1U);
            assert_int_equal(pd_in[0], 0xA5U);
            assert_int_equal(g_last_pd_input_len, 1U);
            assert_int_equal(g_last_pd_input[0], 0xA5U);
            return;
        }
    }

    fail_msg("real iolinki device stack did not reach OPERATE with iolinki-master");
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_master_reaches_operate_with_real_iolinki_device_stack),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
