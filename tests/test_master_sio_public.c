#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>

#include <cmocka.h>

#include "iolinki_master/master.h"

static int g_read_calls;
static int g_read_result;

static int fake_read_cq_line(void)
{
    g_read_calls++;
    return g_read_result;
}

static const iolink_phy_api_t g_phy = {0};

static const iolink_master_config_t g_config = {
    .port_mode = IOLINK_MASTER_PORT_MODE_DI,
    .m_seq_type = IOLINK_MASTER_M_SEQ_TYPE_0,
    .baudrate = IOLINK_BAUDRATE_COM3,
    .read_cq_line = fake_read_cq_line,
};

static int reset_fake_io(void** state)
{
    (void)state;
    g_read_calls = 0;
    g_read_result = 1;
    return 0;
}

static void test_get_di_reads_configured_cq_reader_for_di_ports(void** state)
{
    iolink_master_port_t port;
    bool level = false;

    (void)state;

    assert_int_equal(iolink_master_init(&port, &g_phy, &g_config), IOLINK_MASTER_STATUS_OK);
    assert_int_equal(iolink_master_get_di(&port, &level), IOLINK_MASTER_STATUS_OK);
    assert_true(level);
    assert_int_equal(g_read_calls, 1);

    g_read_result = 0;
    assert_int_equal(iolink_master_get_di(&port, &level), IOLINK_MASTER_STATUS_OK);
    assert_false(level);
    assert_int_equal(g_read_calls, 2);
}

static void test_get_di_rejects_invalid_args_wrong_mode_and_missing_reader(void** state)
{
    iolink_master_port_t port;
    iolink_master_config_t config = g_config;
    bool level = false;

    (void)state;

    assert_int_equal(iolink_master_get_di(NULL, &level), IOLINK_MASTER_ERR_INVALID_ARG);

    assert_int_equal(iolink_master_init(&port, &g_phy, &config), IOLINK_MASTER_STATUS_OK);
    assert_int_equal(iolink_master_get_di(&port, NULL), IOLINK_MASTER_ERR_INVALID_ARG);

    config.port_mode = IOLINK_MASTER_PORT_MODE_DQ;
    assert_int_equal(iolink_master_init(&port, &g_phy, &config), IOLINK_MASTER_STATUS_OK);
    assert_int_equal(iolink_master_get_di(&port, &level), IOLINK_MASTER_SIO_ERR_WRONG_MODE);

    config.port_mode = IOLINK_MASTER_PORT_MODE_DI;
    config.read_cq_line = NULL;
    assert_int_equal(iolink_master_init(&port, &g_phy, &config), IOLINK_MASTER_STATUS_OK);
    assert_int_equal(iolink_master_get_di(&port, &level), IOLINK_MASTER_SIO_ERR_UNSUPPORTED_PHY);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_get_di_reads_configured_cq_reader_for_di_ports,
                               reset_fake_io),
        cmocka_unit_test_setup(test_get_di_rejects_invalid_args_wrong_mode_and_missing_reader,
                               reset_fake_io),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
