#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include <cmocka.h>

#include "iolinki/frame.h"
#include "iolinki/protocol.h"
#include "iolinki_master/master.h"

static int g_init_calls;
static int g_set_mode_calls;
static int g_set_baudrate_calls;
static int g_send_calls;
static int g_forced_send_return;
static iolink_phy_mode_t g_last_mode;
static iolink_baudrate_t g_last_baudrate;
static uint8_t g_sent[8][64];
static size_t g_sent_len[8];

static int fake_phy_init(void)
{
    g_init_calls++;
    return 0;
}

static void fake_phy_set_mode(iolink_phy_mode_t mode)
{
    g_set_mode_calls++;
    g_last_mode = mode;
}

static void fake_phy_set_baudrate(iolink_baudrate_t baudrate)
{
    g_set_baudrate_calls++;
    g_last_baudrate = baudrate;
}

static int fake_phy_send(const uint8_t* data, size_t len)
{
    assert_non_null(data);
    assert_in_range(len, 1U, sizeof(g_sent[0]));
    assert_in_range(g_send_calls, 0, 7);

    memcpy(g_sent[g_send_calls], data, len);
    g_sent_len[g_send_calls] = len;
    g_send_calls++;

    if(g_forced_send_return != INT_MIN)
    {
        return g_forced_send_return;
    }

    return (int)len;
}

static const iolink_phy_api_t g_fake_phy = {
    .init = fake_phy_init,
    .set_mode = fake_phy_set_mode,
    .set_baudrate = fake_phy_set_baudrate,
    .send = fake_phy_send,
};

static const iolink_master_config_t g_config = {
    .m_seq_type = IOLINK_MASTER_M_SEQ_TYPE_2_1,
    .baudrate = IOLINK_BAUDRATE_COM3,
    .min_cycle_time = 20U,
    .pd_in_len = 4U,
    .pd_out_len = 2U,
};

static int reset_fake_phy(void** state)
{
    (void)state;
    g_init_calls = 0;
    g_set_mode_calls = 0;
    g_set_baudrate_calls = 0;
    g_send_calls = 0;
    g_forced_send_return = INT_MIN;
    g_last_mode = IOLINK_PHY_MODE_INACTIVE;
    g_last_baudrate = IOLINK_BAUDRATE_COM1;
    memset(g_sent, 0, sizeof(g_sent));
    memset(g_sent_len, 0, sizeof(g_sent_len));
    return 0;
}

static void test_init_rejects_null_args(void** state)
{
    iolink_master_port_t port;

    (void)state;

    assert_int_equal(iolink_master_init(NULL, &g_fake_phy, &g_config), -1);
    assert_int_equal(iolink_master_init(&port, NULL, &g_config), -1);
    assert_int_equal(iolink_master_init(&port, &g_fake_phy, NULL), -1);
}

static void test_valid_init_sets_startup_state(void** state)
{
    iolink_master_port_t port;

    (void)state;

    assert_int_equal(iolink_master_init(&port, &g_fake_phy, &g_config), 0);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_STARTUP);
    assert_int_equal(port.od_len, 2);
    assert_int_equal(port.pd_in_len, g_config.pd_in_len);
    assert_int_equal(g_init_calls, 1);
    assert_int_equal(g_set_baudrate_calls, 1);
    assert_int_equal(g_last_baudrate, IOLINK_BAUDRATE_COM3);
    assert_int_equal(g_set_mode_calls, 1);
    assert_int_equal(g_last_mode, IOLINK_PHY_MODE_SDCI);
}

static void test_init_rejects_oversized_pd_in_len(void** state)
{
    iolink_master_port_t port;
    iolink_master_config_t config = g_config;

    (void)state;

    config.pd_in_len = (uint8_t)(IOLINK_PD_IN_MAX_SIZE + 1U);

    assert_int_equal(iolink_master_init(&port, &g_fake_phy, &config), -1);
    assert_int_equal(g_init_calls, 0);
}

static void test_init_rejects_oversized_pd_out_len(void** state)
{
    iolink_master_port_t port;
    iolink_master_config_t config = g_config;

    (void)state;

    config.pd_out_len = (uint8_t)(IOLINK_PD_OUT_MAX_SIZE + 1U);

    assert_int_equal(iolink_master_init(&port, &g_fake_phy, &config), -1);
    assert_int_equal(g_init_calls, 0);
}

static void test_get_pd_in_too_small_exposes_required_length(void** state)
{
    iolink_master_port_t port;
    uint8_t buffer[2] = {0U, 0U};
    uint8_t out_len = 0U;

    (void)state;

    assert_int_equal(iolink_master_init(&port, &g_fake_phy, &g_config), 0);
    assert_int_equal(iolink_master_get_pd_in(&port, buffer, sizeof(buffer), &out_len), -2);
    assert_int_equal(out_len, g_config.pd_in_len);
}

static void test_get_pd_in_invalid_does_not_copy_stale_data(void** state)
{
    iolink_master_port_t port;
    uint8_t buffer[4] = {0xAAU, 0xAAU, 0xAAU, 0xAAU};
    uint8_t out_len = 0U;

    (void)state;

    assert_int_equal(iolink_master_init(&port, &g_fake_phy, &g_config), 0);
    port.pd_in[0] = 0x11U;
    port.pd_in[1] = 0x22U;
    port.pd_in[2] = 0x33U;
    port.pd_in[3] = 0x44U;
    port.pd_valid = false;

    assert_int_equal(iolink_master_get_pd_in(&port, buffer, sizeof(buffer), &out_len), 1);
    assert_int_equal(out_len, g_config.pd_in_len);
    assert_int_equal(buffer[0], 0xAAU);
    assert_int_equal(buffer[1], 0xAAU);
    assert_int_equal(buffer[2], 0xAAU);
    assert_int_equal(buffer[3], 0xAAU);
}

static void test_process_startup_enters_preoperate_before_operate_and_sends_cyclic_frame(void** state)
{
    iolink_master_port_t port;
    const uint8_t pd_out[] = {0x11U, 0x22U};
    uint8_t expected[8] = {0U};
    int expected_len;

    (void)state;

    assert_int_equal(iolink_master_init(&port, &g_fake_phy, &g_config), 0);
    assert_int_equal(iolink_master_set_pd_out(&port, pd_out, sizeof(pd_out)), 0);

    iolink_master_process(&port);
    assert_int_equal(g_send_calls, 1);
    assert_int_equal(g_sent_len[0], 1U);
    assert_int_equal(g_sent[0][0], 0x55U);

    iolink_master_process(&port);
    expected_len = iolink_frame_encode_type0(0x00U, expected, sizeof(expected));
    assert_int_equal(expected_len, 2);
    assert_int_equal(g_send_calls, 2);
    assert_int_equal(g_sent_len[1], (size_t)expected_len);
    assert_memory_equal(g_sent[1], expected, (size_t)expected_len);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_PREOPERATE);

    iolink_master_process(&port);
    expected_len = iolink_frame_encode_type0(IOLINK_MC_TRANSITION_COMMAND, expected, sizeof(expected));
    assert_int_equal(expected_len, 2);
    assert_int_equal(g_send_calls, 3);
    assert_int_equal(g_sent_len[2], (size_t)expected_len);
    assert_memory_equal(g_sent[2], expected, (size_t)expected_len);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_OPERATE);

    iolink_master_process(&port);
    expected_len = iolink_frame_encode_type1_cycle(pd_out,
                                                   sizeof(pd_out),
                                                   port.od_len,
                                                   expected,
                                                   sizeof(expected));
    assert_int_equal(expected_len, 7);
    assert_int_equal(g_send_calls, 4);
    assert_int_equal(g_sent_len[3], (size_t)expected_len);
    assert_memory_equal(g_sent[3], expected, (size_t)expected_len);
    assert_int_equal(port.cycle_count, 1U);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_OPERATE);
    assert_true(g_send_calls >= 4);
}

static void test_process_partial_send_enters_error_state(void** state)
{
    iolink_master_port_t port;

    (void)state;

    assert_int_equal(iolink_master_init(&port, &g_fake_phy, &g_config), 0);
    iolink_master_process(&port);
    assert_int_equal(port.startup_step, 1U);

    g_forced_send_return = 1;
    iolink_master_process(&port);

    assert_int_equal(g_send_calls, 2);
    assert_int_equal(port.startup_step, 1U);
    assert_int_equal(port.send_errors, 1U);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_ERROR);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_init_rejects_null_args, reset_fake_phy),
        cmocka_unit_test_setup(test_valid_init_sets_startup_state, reset_fake_phy),
        cmocka_unit_test_setup(test_init_rejects_oversized_pd_in_len, reset_fake_phy),
        cmocka_unit_test_setup(test_init_rejects_oversized_pd_out_len, reset_fake_phy),
        cmocka_unit_test_setup(test_get_pd_in_too_small_exposes_required_length, reset_fake_phy),
        cmocka_unit_test_setup(test_get_pd_in_invalid_does_not_copy_stale_data, reset_fake_phy),
        cmocka_unit_test_setup(
            test_process_startup_enters_preoperate_before_operate_and_sends_cyclic_frame,
                               reset_fake_phy),
        cmocka_unit_test_setup(test_process_partial_send_enters_error_state, reset_fake_phy),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
