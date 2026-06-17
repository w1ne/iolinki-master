#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include <cmocka.h>

#include "iolinki/crc.h"
#include "iolinki/protocol.h"
#include "iolinki_master/master.h"

static uint8_t g_rx_queue[16];
static uint8_t g_rx_len;
static uint8_t g_rx_pos;
static int g_send_calls;
static uint8_t g_sent[8][64];
static size_t g_sent_len[8];

static int fake_send(const uint8_t* data, size_t len)
{
    assert_non_null(data);
    assert_in_range(g_send_calls, 0, 7);
    assert_in_range(len, 1U, sizeof(g_sent[0]));

    memcpy(g_sent[g_send_calls], data, len);
    g_sent_len[g_send_calls] = len;
    g_send_calls++;
    return (int)len;
}

static int fake_recv_byte(uint8_t* byte)
{
    assert_non_null(byte);

    if(g_rx_pos >= g_rx_len)
    {
        return 0;
    }

    *byte = g_rx_queue[g_rx_pos++];
    return 1;
}

static void queue_bytes(const uint8_t* data, uint8_t len)
{
    memcpy(g_rx_queue, data, len);
    g_rx_len = len;
    g_rx_pos = 0U;
}

static const iolink_phy_api_t g_phy = {
    .send = fake_send,
    .recv_byte = fake_recv_byte,
};

static const iolink_master_config_t g_config = {
    .port_mode = IOLINK_MASTER_PORT_MODE_IOLINK,
    .m_seq_type = IOLINK_MASTER_M_SEQ_TYPE_2_1,
    .baudrate = IOLINK_BAUDRATE_COM3,
    .min_cycle_time = 20U,
    .pd_in_len = 1U,
    .pd_out_len = 0U,
    .auto_baudrate = false,
};

static int reset_fixture(void** state)
{
    (void)state;
    memset(g_rx_queue, 0, sizeof(g_rx_queue));
    memset(g_sent, 0, sizeof(g_sent));
    memset(g_sent_len, 0, sizeof(g_sent_len));
    g_rx_len = 0U;
    g_rx_pos = 0U;
    g_send_calls = 0;
    return 0;
}

static void test_tick_sends_startup_frame_when_no_rx(void** state)
{
    iolink_master_port_t port;

    (void)state;

    assert_int_equal(iolink_master_init(&port, &g_phy, &g_config), 0);
    assert_int_equal(iolink_master_tick(&port, false), 0);
    assert_int_equal(g_send_calls, 1);
    assert_int_equal(g_sent_len[0], 1U);
    assert_int_equal(g_sent[0][0], 0x55U);
}

static void test_tick_drains_rx_before_sending_next_frame(void** state)
{
    iolink_master_port_t port;
    uint8_t startup_resp[2] = {0U};

    (void)state;

    assert_int_equal(iolink_master_init(&port, &g_phy, &g_config), 0);
    iolink_master_process(&port);
    iolink_master_process(&port);
    assert_int_equal(port.startup.step, 2U);

    startup_resp[0] = 0x00U;
    startup_resp[1] = iolink_checksum_ck(startup_resp[0], 0U);
    queue_bytes(startup_resp, sizeof(startup_resp));

    assert_int_equal(iolink_master_tick(&port, false), 1);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_OPERATE);
    assert_int_equal(g_send_calls, 3);
    assert_int_equal(g_sent_len[2], 2U);
    assert_int_equal(g_sent[2][0], IOLINK_MC_TRANSITION_COMMAND);
}

static void test_tick_applies_timeout_before_transmit(void** state)
{
    iolink_master_port_t port;

    (void)state;

    assert_int_equal(iolink_master_init(&port, &g_phy, &g_config), 0);
    port.state = IOLINK_MASTER_STATE_OPERATE;
    port.diagnostics.rx_retry_count = 2U;

    assert_int_equal(iolink_master_tick(&port, true), -2);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_ERROR);
    assert_int_equal(g_send_calls, 0);
}

static void test_tick_rejects_null_port(void** state)
{
    (void)state;

    assert_int_equal(iolink_master_tick(NULL, false), -1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_tick_sends_startup_frame_when_no_rx, reset_fixture),
        cmocka_unit_test_setup(test_tick_drains_rx_before_sending_next_frame, reset_fixture),
        cmocka_unit_test_setup(test_tick_applies_timeout_before_transmit, reset_fixture),
        cmocka_unit_test_setup(test_tick_rejects_null_port, reset_fixture),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
