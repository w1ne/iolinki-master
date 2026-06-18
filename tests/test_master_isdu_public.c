#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "iolinki/crc.h"
#include "iolinki/protocol.h"
#include "iolinki_master/master.h"

static int g_send_calls;
static uint8_t g_sent[16][8];
static size_t g_sent_len[16];

static int fake_send(const uint8_t* data, size_t len)
{
    assert_non_null(data);
    assert_in_range(g_send_calls, 0, 15);
    assert_in_range(len, 1U, sizeof(g_sent[0]));

    memcpy(g_sent[g_send_calls], data, len);
    g_sent_len[g_send_calls] = len;
    g_send_calls++;
    return (int)len;
}

static const iolink_phy_api_t g_phy = {
    .send = fake_send,
};

static const iolink_master_config_t g_config = {
    .port_mode = IOLINK_MASTER_PORT_MODE_IOLINK,
    .m_seq_type = IOLINK_MASTER_M_SEQ_TYPE_0,
    .baudrate = IOLINK_BAUDRATE_COM3,
    .min_cycle_time = 20U,
};

static int reset_fixture(void** state)
{
    (void)state;
    g_send_calls = 0;
    memset(g_sent, 0, sizeof(g_sent));
    memset(g_sent_len, 0, sizeof(g_sent_len));
    return 0;
}

static void feed_type0_byte(iolink_master_port_t* port, uint8_t byte)
{
    uint8_t frame[2];

    frame[0] = byte;
    frame[1] = iolink_checksum_ck(frame[0], 0U);
    assert_int_equal(iolink_master_on_rx(port, frame, sizeof(frame)), IOLINK_MASTER_STATUS_OK);
}

static void enter_type0_operate(iolink_master_port_t* port)
{
    assert_int_equal(iolink_master_init(port, &g_phy, &g_config), IOLINK_MASTER_STATUS_OK);
    assert_int_equal(iolink_master_tick_event(port, IOLINK_MASTER_TICK_CYCLE_DUE),
                     IOLINK_MASTER_STATUS_OK);
    assert_int_equal(g_sent_len[0], 1U);
    assert_int_equal(g_sent[0][0], 0x55U);

    assert_int_equal(iolink_master_tick_event(port, IOLINK_MASTER_TICK_CYCLE_DUE),
                     IOLINK_MASTER_STATUS_OK);
    feed_type0_byte(port, 0x00U);
    assert_int_equal(iolink_master_get_state(port), IOLINK_MASTER_STATE_PREOPERATE);

    assert_int_equal(iolink_master_tick_event(port, IOLINK_MASTER_TICK_CYCLE_DUE),
                     IOLINK_MASTER_STATUS_OK);
    assert_int_equal(iolink_master_get_state(port), IOLINK_MASTER_STATE_OPERATE);
}

static void assert_last_type0_request(uint8_t expected_od)
{
    assert_true(g_send_calls > 0);
    assert_int_equal(g_sent_len[g_send_calls - 1], IOLINK_M_SEQ_TYPE0_LEN);
    assert_int_equal(g_sent[g_send_calls - 1][0], expected_od);
    assert_int_equal(g_sent[g_send_calls - 1][1], iolink_checksum_ck(expected_od, 0U));
}

static void test_public_type0_isdu_read_completes_without_private_state(void** state)
{
    iolink_master_port_t port;
    uint8_t data[4] = {0U};
    uint8_t len = sizeof(data);

    (void)state;

    enter_type0_operate(&port);

    assert_int_equal(iolink_master_read_isdu(&port, 0x1234U, 0x56U, data, &len),
                     IOLINK_MASTER_STATUS_PENDING);

    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE),
                     IOLINK_MASTER_STATUS_OK);
    assert_last_type0_request(IOLINK_ISDU_CTRL_START);

    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE),
                     IOLINK_MASTER_STATUS_OK);
    assert_last_type0_request(IOLINK_ISDU_SERVICE_READ << 4);

    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE),
                     IOLINK_MASTER_STATUS_OK);
    assert_last_type0_request(0x01U);

    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE),
                     IOLINK_MASTER_STATUS_OK);
    assert_last_type0_request(0x12U);

    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE),
                     IOLINK_MASTER_STATUS_OK);
    assert_last_type0_request(0x02U);

    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE),
                     IOLINK_MASTER_STATUS_OK);
    assert_last_type0_request(0x34U);

    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE),
                     IOLINK_MASTER_STATUS_OK);
    assert_last_type0_request((uint8_t)(IOLINK_ISDU_CTRL_LAST | 0x03U));

    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE),
                     IOLINK_MASTER_STATUS_OK);
    assert_last_type0_request(0x56U);

    feed_type0_byte(&port, IOLINK_ISDU_CTRL_START);
    feed_type0_byte(&port, 0xCAU);
    feed_type0_byte(&port, (uint8_t)(IOLINK_ISDU_CTRL_LAST | 0x01U));
    feed_type0_byte(&port, 0xFEU);

    assert_int_equal(iolink_master_read_isdu(&port, 0x1234U, 0x56U, data, &len),
                     IOLINK_MASTER_STATUS_OK);
    assert_int_equal(len, 2U);
    assert_int_equal(data[0], 0xCAU);
    assert_int_equal(data[1], 0xFEU);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_public_type0_isdu_read_completes_without_private_state,
                               reset_fixture),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
