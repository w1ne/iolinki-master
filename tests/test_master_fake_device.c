#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "fake_iolink_device.h"
#include "iolinki_master/master.h"

static const iolink_master_config_t g_config = {
    .port_mode = IOLINK_MASTER_PORT_MODE_IOLINK,
    .m_seq_type = IOLINK_MASTER_M_SEQ_TYPE_1_1,
    .baudrate = IOLINK_BAUDRATE_COM3,
    .min_cycle_time = 20U,
    .pd_in_len = 1U,
    .pd_out_len = 0U,
    .auto_baudrate = false,
};

static int reset_fixture(void** state)
{
    (void)state;
    fake_iolink_device_reset(0xA5U, 1U, 1U);
    return 0;
}

static void test_fake_device_drives_startup_and_paced_pd_cycle(void** state)
{
    iolink_master_port_t port;
    uint8_t pd[1] = {0U};
    uint8_t len = 0U;

    (void)state;

    assert_int_equal(iolink_master_init(&port, fake_iolink_device_phy(), &g_config), 0);

    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE), 0);
    assert_int_equal(fake_iolink_device_wakeup_count(), 1U);

    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE), 0);
    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_NONE), 1);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_PREOPERATE);

    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE), 0);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_OPERATE);
    assert_int_equal(fake_iolink_device_transition_count(), 1U);

    assert_int_equal(iolink_master_tick_at(&port, IOLINK_MASTER_TICK_CYCLE_DUE, 100U), 0);
    assert_int_equal(fake_iolink_device_operate_cycle_count(), 1U);

    assert_int_equal(iolink_master_tick_at(&port, IOLINK_MASTER_TICK_NONE, 101U), 1);
    assert_int_equal(iolink_master_get_pd_in(&port, pd, sizeof(pd), &len), 0);
    assert_int_equal(len, 1U);
    assert_int_equal(pd[0], 0xA5U);

    assert_int_equal(iolink_master_tick_at(&port, IOLINK_MASTER_TICK_CYCLE_DUE, 119U), 0);
    assert_int_equal(fake_iolink_device_operate_cycle_count(), 1U);

    assert_int_equal(iolink_master_tick_at(&port, IOLINK_MASTER_TICK_CYCLE_DUE, 120U), 0);
    assert_int_equal(fake_iolink_device_operate_cycle_count(), 2U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_fake_device_drives_startup_and_paced_pd_cycle,
                               reset_fixture),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
