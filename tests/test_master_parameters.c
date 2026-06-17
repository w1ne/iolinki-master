#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "iolinki_master/master.h"

static const uint8_t g_page1[] = {
    0x00U, /* MasterCommand */
    0x00U, /* MasterCycleTime */
    0x0AU, /* MinCycleTime */
    0x0BU, /* M-sequenceCapability: ISDU + operate code 5 */
    0x11U, /* RevisionID */
    0x10U, /* ProcessDataIn: 16 bits */
    0x83U, /* ProcessDataOut: 4 octets */
    0x12U, /* VendorID MSB */
    0x34U, /* VendorID LSB */
    0x56U, /* DeviceID high */
    0x78U, /* DeviceID mid */
    0x9AU, /* DeviceID low */
    0x00U,
    0x00U,
    0x00U,
    0x00U,
};

static int null_phy_init(void)
{
    return 0;
}

static const iolink_phy_api_t g_phy = {
    .init = null_phy_init,
};

static const iolink_master_config_t g_config = {
    .port_mode = IOLINK_MASTER_PORT_MODE_IOLINK,
    .m_seq_type = IOLINK_MASTER_M_SEQ_TYPE_2_1,
    .baudrate = IOLINK_BAUDRATE_COM3,
    .min_cycle_time = 20U,
    .pd_in_len = 2U,
    .pd_out_len = 4U,
};

static void test_parse_direct_parameter_page1_decodes_standard_fields(void** state)
{
    iolink_master_device_info_t info;

    (void)state;

    memset(&info, 0, sizeof(info));

    assert_int_equal(iolink_master_parse_direct_parameter_page1(g_page1, sizeof(g_page1), &info),
                     0);
    assert_true(info.valid);
    assert_int_equal(info.min_cycle_time, 0x0AU);
    assert_int_equal(info.revision_id, 0x11U);
    assert_true(info.isdu_supported);
    assert_int_equal(info.operate_mseq_code, 5U);
    assert_int_equal(info.preoperate_mseq_code, 0U);
    assert_int_equal(info.pd_in_descriptor, 0x10U);
    assert_int_equal(info.pd_out_descriptor, 0x83U);
    assert_int_equal(info.pd_in_len, 2U);
    assert_int_equal(info.pd_out_len, 4U);
    assert_int_equal(info.vendor_id, 0x1234U);
    assert_int_equal(info.device_id, 0x56789AU);
}

static void test_parse_direct_parameter_page1_decodes_zero_and_small_bit_lengths(void** state)
{
    uint8_t page[16] = {0U};
    iolink_master_device_info_t info;

    (void)state;

    page[0x05] = 0x08U;
    page[0x06] = 0x00U;

    assert_int_equal(iolink_master_parse_direct_parameter_page1(page, sizeof(page), &info), 0);
    assert_int_equal(info.pd_in_len, 1U);
    assert_int_equal(info.pd_out_len, 0U);
}

static void test_parse_direct_parameter_page1_rejects_invalid_args(void** state)
{
    iolink_master_device_info_t info;

    (void)state;

    assert_int_equal(iolink_master_parse_direct_parameter_page1(NULL, sizeof(g_page1), &info),
                     -1);
    assert_int_equal(iolink_master_parse_direct_parameter_page1(g_page1, sizeof(g_page1), NULL),
                     -1);
    assert_int_equal(iolink_master_parse_direct_parameter_page1(g_page1,
                                                                (uint8_t)(sizeof(g_page1) - 1U),
                                                                &info),
                     -2);
}

static void test_apply_direct_parameter_page1_latches_info_on_port(void** state)
{
    iolink_master_port_t port;
    iolink_master_device_info_t info;

    (void)state;

    assert_int_equal(iolink_master_init(&port, &g_phy, &g_config), 0);
    assert_int_equal(iolink_master_apply_direct_parameter_page1(&port, g_page1, sizeof(g_page1)),
                     0);
    assert_int_equal(iolink_master_get_device_info(&port, &info), 0);
    assert_true(info.valid);
    assert_int_equal(info.vendor_id, 0x1234U);
    assert_int_equal(info.device_id, 0x56789AU);
    assert_int_equal(info.pd_in_len, 2U);
    assert_int_equal(info.pd_out_len, 4U);
}

static void test_get_device_info_rejects_invalid_or_unavailable_info(void** state)
{
    iolink_master_port_t port;
    iolink_master_device_info_t info;

    (void)state;

    assert_int_equal(iolink_master_get_device_info(NULL, &info), -1);
    assert_int_equal(iolink_master_get_device_info(&port, NULL), -1);

    assert_int_equal(iolink_master_init(&port, &g_phy, &g_config), 0);
    assert_int_equal(iolink_master_get_device_info(&port, &info), 1);
    assert_false(info.valid);
}

static void test_validate_device_info_accepts_matching_configuration(void** state)
{
    uint8_t page[16];
    iolink_master_port_t port;

    (void)state;

    memcpy(page, g_page1, sizeof(page));
    page[0x02] = 10U;
    page[0x03] = 0x01U; /* ISDU supported, operate M-sequence code 0. */

    assert_int_equal(iolink_master_init(&port, &g_phy, &g_config), 0);
    assert_int_equal(iolink_master_apply_direct_parameter_page1(&port, page, sizeof(page)), 0);
    assert_int_equal(iolink_master_validate_device_info(&port), 0);
}

static void test_validate_device_info_rejects_missing_or_invalid_info(void** state)
{
    iolink_master_port_t port;
    uint8_t page[16];

    (void)state;

    assert_int_equal(iolink_master_validate_device_info(NULL), -1);

    assert_int_equal(iolink_master_init(&port, &g_phy, &g_config), 0);
    assert_int_equal(iolink_master_validate_device_info(&port), 1);

    memcpy(page, g_page1, sizeof(page));
    page[0x04] = 0x22U;
    assert_int_equal(iolink_master_apply_direct_parameter_page1(&port, page, sizeof(page)), 0);
    assert_int_equal(iolink_master_validate_device_info(&port), -2);
}

static void test_validate_device_info_rejects_incompatible_cycle_pd_and_mseq(void** state)
{
    uint8_t page[16];
    iolink_master_port_t port;

    (void)state;

    memcpy(page, g_page1, sizeof(page));
    page[0x02] = 21U;
    assert_int_equal(iolink_master_init(&port, &g_phy, &g_config), 0);
    assert_int_equal(iolink_master_apply_direct_parameter_page1(&port, page, sizeof(page)), 0);
    assert_int_equal(iolink_master_validate_device_info(&port), -3);

    memcpy(page, g_page1, sizeof(page));
    page[0x05] = 0x18U;
    assert_int_equal(iolink_master_apply_direct_parameter_page1(&port, page, sizeof(page)), 0);
    assert_int_equal(iolink_master_validate_device_info(&port), -4);

    memcpy(page, g_page1, sizeof(page));
    page[0x03] = 0x03U; /* ISDU supported, operate M-sequence code 1. */
    assert_int_equal(iolink_master_apply_direct_parameter_page1(&port, page, sizeof(page)), 0);
    assert_int_equal(iolink_master_validate_device_info(&port), -5);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_parse_direct_parameter_page1_decodes_standard_fields),
        cmocka_unit_test(test_parse_direct_parameter_page1_decodes_zero_and_small_bit_lengths),
        cmocka_unit_test(test_parse_direct_parameter_page1_rejects_invalid_args),
        cmocka_unit_test(test_apply_direct_parameter_page1_latches_info_on_port),
        cmocka_unit_test(test_get_device_info_rejects_invalid_or_unavailable_info),
        cmocka_unit_test(test_validate_device_info_accepts_matching_configuration),
        cmocka_unit_test(test_validate_device_info_rejects_missing_or_invalid_info),
        cmocka_unit_test(test_validate_device_info_rejects_incompatible_cycle_pd_and_mseq),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
