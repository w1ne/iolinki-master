#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "iolinki_master/master.h"

static const iolink_phy_api_t g_empty_phy = {0};

static const iolink_master_config_t g_config = {
    .m_seq_type = IOLINK_MASTER_M_SEQ_TYPE_2_1,
    .baudrate = IOLINK_BAUDRATE_COM3,
    .min_cycle_time = 20U,
    .pd_in_len = 4U,
    .pd_out_len = 2U,
};

static void test_on_rx_valid_response_latches_pd(void** state)
{
    iolink_master_port_t port = {0};
    const uint8_t frame[] = {0x20U, 0xA5U, 0x00U, 0x0DU};
    uint8_t pd[1] = {0U};
    uint8_t len = 0U;

    (void)state;

    port.config.pd_in_len = 1U;
    port.od_len = 1U;

    assert_int_equal(iolink_master_on_rx(&port, frame, sizeof(frame)), 0);
    assert_int_equal(iolink_master_get_pd_in(&port, pd, sizeof(pd), &len), 0);
    assert_int_equal(len, 1U);
    assert_int_equal(pd[0], 0xA5U);
}

static void test_on_rx_bad_checksum_returns_error_and_increments_count(void** state)
{
    iolink_master_port_t port = {0};
    const uint8_t frame[] = {0x20U, 0xA5U, 0x00U, 0x00U};

    (void)state;

    port.config.pd_in_len = 1U;
    port.od_len = 1U;

    assert_int_equal(iolink_master_on_rx(&port, frame, sizeof(frame)), -3);
    assert_int_equal(port.checksum_errors, 1U);
}

static void test_on_rx_bad_checksum_retries_twice_before_error_state(void** state)
{
    iolink_master_port_t port = {0};
    const uint8_t frame[] = {0x20U, 0xA5U, 0x00U, 0x00U};

    (void)state;

    port.state = IOLINK_MASTER_STATE_OPERATE;
    port.config.pd_in_len = 1U;
    port.od_len = 1U;

    assert_int_equal(iolink_master_on_rx(&port, frame, sizeof(frame)), -3);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_OPERATE);

    assert_int_equal(iolink_master_on_rx(&port, frame, sizeof(frame)), -3);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_OPERATE);

    assert_int_equal(iolink_master_on_rx(&port, frame, sizeof(frame)), -3);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_ERROR);
    assert_int_equal(port.checksum_errors, 3U);
}

static void test_on_rx_valid_response_resets_checksum_retry_count(void** state)
{
    iolink_master_port_t port = {0};
    const uint8_t bad_frame[] = {0x20U, 0xA5U, 0x00U, 0x00U};
    const uint8_t good_frame[] = {0x20U, 0xA5U, 0x00U, 0x0DU};

    (void)state;

    port.state = IOLINK_MASTER_STATE_OPERATE;
    port.config.pd_in_len = 1U;
    port.od_len = 1U;

    assert_int_equal(iolink_master_on_rx(&port, bad_frame, sizeof(bad_frame)), -3);
    assert_int_equal(iolink_master_on_rx(&port, good_frame, sizeof(good_frame)), 0);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_OPERATE);

    assert_int_equal(iolink_master_on_rx(&port, bad_frame, sizeof(bad_frame)), -3);
    assert_int_equal(iolink_master_on_rx(&port, bad_frame, sizeof(bad_frame)), -3);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_OPERATE);
}

static void test_on_rx_malformed_frame_returns_decode_error(void** state)
{
    iolink_master_port_t port = {0};
    const uint8_t frame[] = {0x20U, 0xA5U};

    (void)state;

    port.config.pd_in_len = 1U;
    port.od_len = 1U;

    assert_int_equal(iolink_master_on_rx(&port, frame, sizeof(frame)), -2);
    assert_int_equal(port.checksum_errors, 0U);
}

static void test_on_rx_rejects_invalid_args(void** state)
{
    iolink_master_port_t port = {0};
    const uint8_t frame[] = {0x20U, 0xA5U, 0x00U, 0x0DU};

    (void)state;

    assert_int_equal(iolink_master_on_rx(NULL, frame, sizeof(frame)), -1);
    assert_int_equal(iolink_master_on_rx(&port, NULL, sizeof(frame)), -1);
    assert_int_equal(iolink_master_on_rx(&port, frame, 0U), -1);
}

static void test_set_pd_out_rejects_invalid_args(void** state)
{
    iolink_master_port_t port;
    const uint8_t pd_out[] = {0x11U, 0x22U};

    (void)state;

    assert_int_equal(iolink_master_init(&port, &g_empty_phy, &g_config), 0);
    assert_int_equal(iolink_master_set_pd_out(NULL, pd_out, sizeof(pd_out)), -1);
    assert_int_equal(iolink_master_set_pd_out(&port, NULL, sizeof(pd_out)), -1);
}

static void test_set_pd_out_rejects_length_mismatch(void** state)
{
    iolink_master_port_t port;
    const uint8_t short_pd_out[] = {0x11U};
    const uint8_t long_pd_out[] = {0x11U, 0x22U, 0x33U};

    (void)state;

    assert_int_equal(iolink_master_init(&port, &g_empty_phy, &g_config), 0);
    assert_int_equal(iolink_master_set_pd_out(&port, short_pd_out, sizeof(short_pd_out)), -2);
    assert_int_equal(iolink_master_set_pd_out(&port, long_pd_out, sizeof(long_pd_out)), -2);
}

static void test_set_pd_out_accepts_zero_length_when_configured(void** state)
{
    iolink_master_port_t port;
    iolink_master_config_t config = g_config;

    (void)state;

    config.pd_out_len = 0U;

    assert_int_equal(iolink_master_init(&port, &g_empty_phy, &config), 0);
    assert_int_equal(iolink_master_set_pd_out(&port, NULL, 0U), 0);
    assert_int_equal(port.pd_out_len, 0U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_on_rx_valid_response_latches_pd),
        cmocka_unit_test(test_on_rx_bad_checksum_returns_error_and_increments_count),
        cmocka_unit_test(test_on_rx_bad_checksum_retries_twice_before_error_state),
        cmocka_unit_test(test_on_rx_valid_response_resets_checksum_retry_count),
        cmocka_unit_test(test_on_rx_malformed_frame_returns_decode_error),
        cmocka_unit_test(test_on_rx_rejects_invalid_args),
        cmocka_unit_test(test_set_pd_out_rejects_invalid_args),
        cmocka_unit_test(test_set_pd_out_rejects_length_mismatch),
        cmocka_unit_test(test_set_pd_out_accepts_zero_length_when_configured),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
