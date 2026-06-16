#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "iolinki_master/master.h"

static void test_read_isdu_rejects_invalid_args(void** state)
{
    iolink_master_port_t port = {0};
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);

    (void)state;

    assert_int_equal(iolink_master_read_isdu(NULL, 0x0010U, 0U, data, &len), -1);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, NULL, &len), -1);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, NULL), -1);
}

static void test_read_isdu_returns_pending_for_valid_request(void** state)
{
    iolink_master_port_t port = {0};
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);

    (void)state;

    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), 1);
}

static void test_write_isdu_rejects_invalid_args(void** state)
{
    iolink_master_port_t port = {0};
    const uint8_t data[] = {0x11U, 0x22U};

    (void)state;

    assert_int_equal(iolink_master_write_isdu(NULL, 0x0010U, 0U, data, sizeof(data)), -1);
    assert_int_equal(iolink_master_write_isdu(&port, 0x0010U, 0U, NULL, sizeof(data)), -1);
}

static void test_write_isdu_accepts_valid_and_zero_length_requests(void** state)
{
    iolink_master_port_t port = {0};
    const uint8_t data[] = {0x11U, 0x22U};

    (void)state;

    assert_int_equal(iolink_master_write_isdu(&port, 0x0010U, 0U, data, sizeof(data)), 1);
    assert_int_equal(iolink_master_write_isdu(&port, 0x0010U, 0U, NULL, 0U), 1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_read_isdu_rejects_invalid_args),
        cmocka_unit_test(test_read_isdu_returns_pending_for_valid_request),
        cmocka_unit_test(test_write_isdu_rejects_invalid_args),
        cmocka_unit_test(test_write_isdu_accepts_valid_and_zero_length_requests),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
