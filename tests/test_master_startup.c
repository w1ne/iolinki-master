#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "iolinki_master/master.h"

static int g_init_calls;
static int g_set_mode_calls;
static int g_set_baudrate_calls;
static iolink_phy_mode_t g_last_mode;
static iolink_baudrate_t g_last_baudrate;

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

static const iolink_phy_api_t g_fake_phy = {
    .init = fake_phy_init,
    .set_mode = fake_phy_set_mode,
    .set_baudrate = fake_phy_set_baudrate,
};

static const iolink_master_config_t g_config = {
    .m_seq_type = IOLINK_M_SEQ_TYPE_2_1,
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
    g_last_mode = IOLINK_PHY_MODE_INACTIVE;
    g_last_baudrate = IOLINK_BAUDRATE_COM1;
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

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_init_rejects_null_args, reset_fake_phy),
        cmocka_unit_test_setup(test_valid_init_sets_startup_state, reset_fake_phy),
        cmocka_unit_test_setup(test_init_rejects_oversized_pd_in_len, reset_fake_phy),
        cmocka_unit_test_setup(test_init_rejects_oversized_pd_out_len, reset_fake_phy),
        cmocka_unit_test_setup(test_get_pd_in_too_small_exposes_required_length, reset_fake_phy),
        cmocka_unit_test_setup(test_get_pd_in_invalid_does_not_copy_stale_data, reset_fake_phy),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
