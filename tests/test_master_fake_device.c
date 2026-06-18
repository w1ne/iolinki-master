#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "fake_iolink_device.h"
#include "iolinki/protocol.h"
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

static void test_fake_device_exposes_event_pending_status(void** state)
{
    iolink_master_port_t port;
    iolink_master_diagnostics_t diagnostics;

    (void)state;

    fake_iolink_device_set_event_pending(true);

    assert_int_equal(iolink_master_init(&port, fake_iolink_device_phy(), &g_config), 0);
    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE), 0);
    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE), 0);
    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_NONE), 1);
    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE), 0);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_OPERATE);

    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE), 0);
    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_NONE), 1);

    assert_int_equal(iolink_master_get_diagnostics(&port, &diagnostics), 0);
    assert_true(diagnostics.event_pending);
    assert_true((diagnostics.od_status & IOLINK_OD_STATUS_EVENT) != 0U);
}

static void test_fake_device_serves_isdu_object_dictionary_read(void** state)
{
    iolink_master_port_t port;
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);
    const uint8_t object_value[] = {0x4FU, 0x4BU};
    uint8_t i;

    (void)state;

    fake_iolink_device_set_isdu_object(0x0010U, 0U, object_value, sizeof(object_value));

    assert_int_equal(iolink_master_init(&port, fake_iolink_device_phy(), &g_config), 0);
    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE), 0);
    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE), 0);
    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_NONE), 1);
    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE), 0);
    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_OPERATE);

    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len),
                     IOLINK_MASTER_STATUS_PENDING);

    for(i = 0U; i < 11U; i++)
    {
        assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE), 0);
        assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_NONE), 1);
    }

    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len),
                     IOLINK_MASTER_STATUS_OK);
    assert_int_equal(len, 2U);
    assert_int_equal(data[0], 0x4FU);
    assert_int_equal(data[1], 0x4BU);
}

static void test_fake_device_serves_startup_device_validation_page(void** state)
{
    iolink_master_port_t port;
    iolink_master_config_t config = g_config;
    iolink_master_device_info_t info;
    const uint8_t page1[] = {
        0x00U,
        0x00U,
        10U,
        0x03U, /* ISDU supported, operate M-sequence code 1. */
        0x11U,
        0x08U,
        0x00U,
        0x12U,
        0x34U,
        0x56U,
        0x78U,
        0x9AU,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
    };
    uint8_t i;

    (void)state;

    config.validate_device_info = true;
    fake_iolink_device_set_isdu_object(IOLINK_IDX_DIRECT_PARAMETERS_1, 0U, page1, sizeof(page1));

    assert_int_equal(iolink_master_init(&port, fake_iolink_device_phy(), &config), 0);

    for(i = 0U; i < 64U; i++)
    {
        assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE), 0);
        (void)iolink_master_tick_event(&port, IOLINK_MASTER_TICK_NONE);
        if(iolink_master_get_state(&port) == IOLINK_MASTER_STATE_OPERATE)
        {
            break;
        }
    }

    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_OPERATE);
    assert_int_equal(iolink_master_get_device_info(&port, &info), IOLINK_MASTER_STATUS_OK);
    assert_int_equal(info.vendor_id, 0x1234U);
    assert_int_equal(info.device_id, 0x56789AU);
}

static void test_fake_device_keeps_startup_page_and_application_object(void** state)
{
    iolink_master_port_t port;
    iolink_master_config_t config = g_config;
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);
    const uint8_t object_value[] = {0x4FU, 0x4BU};
    const uint8_t page1[] = {
        0x00U,
        0x00U,
        10U,
        0x03U,
        0x11U,
        0x08U,
        0x00U,
        0x12U,
        0x34U,
        0x56U,
        0x78U,
        0x9AU,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
    };
    uint8_t i;

    (void)state;

    config.validate_device_info = true;
    fake_iolink_device_set_isdu_object(IOLINK_IDX_DIRECT_PARAMETERS_1, 0U, page1, sizeof(page1));
    fake_iolink_device_set_isdu_object(0x0010U, 0U, object_value, sizeof(object_value));

    assert_int_equal(iolink_master_init(&port, fake_iolink_device_phy(), &config), 0);

    for(i = 0U; i < 64U; i++)
    {
        assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE), 0);
        (void)iolink_master_tick_event(&port, IOLINK_MASTER_TICK_NONE);
        if(iolink_master_get_state(&port) == IOLINK_MASTER_STATE_OPERATE)
        {
            break;
        }
    }

    assert_int_equal(iolink_master_get_state(&port), IOLINK_MASTER_STATE_OPERATE);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len),
                     IOLINK_MASTER_STATUS_PENDING);

    for(i = 0U; i < 11U; i++)
    {
        assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE), 0);
        assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_NONE), 1);
    }

    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len),
                     IOLINK_MASTER_STATUS_OK);
    assert_int_equal(len, 2U);
    assert_int_equal(data[0], 0x4FU);
    assert_int_equal(data[1], 0x4BU);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_fake_device_drives_startup_and_paced_pd_cycle,
                               reset_fixture),
        cmocka_unit_test_setup(test_fake_device_exposes_event_pending_status,
                               reset_fixture),
        cmocka_unit_test_setup(test_fake_device_serves_isdu_object_dictionary_read,
                               reset_fixture),
        cmocka_unit_test_setup(test_fake_device_serves_startup_device_validation_page,
                               reset_fixture),
        cmocka_unit_test_setup(test_fake_device_keeps_startup_page_and_application_object,
                               reset_fixture),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
