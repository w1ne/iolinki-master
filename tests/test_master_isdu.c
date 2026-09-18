#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include <cmocka.h>

#include "iolinki/crc.h"
#include "iolinki/frame.h"
#include "iolinki/protocol.h"
#include "test_wire_helpers.h"
#include "../src/master_internal.h"

/*
 * Schema conformance (Spec V1.1.5 7.3.6, A.5):
 *  - the ISDU octet stream is assembled in the private request buffer;
 *  - the transport segments it over M-sequences on the ISDU channel with the
 *    FlowCTRL counter in the MC address bits (A.1.2, Table 52);
 *  - the response is validated with the A.5.6 CHKPDU (XOR of every octet = 0).
 *
 * The tests drive fill_od/on_od directly so a wire byte can be asserted without
 * the port framing in the way.
 */

static int g_send_calls;
static int g_forced_send_return;
static uint8_t g_sent[16][64];
static size_t g_sent_len[16];

static int fake_phy_send(void* user, const uint8_t* data, size_t len)
{
    (void)user;
    assert_non_null(data);
    assert_in_range(len, 1U, sizeof(g_sent[0]));
    assert_in_range(g_send_calls, 0, 15);

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
    .send = fake_phy_send,
};

static const iolink_master_config_t g_config = {
    .m_seq_type = IOLINK_MASTER_M_SEQ_TYPE_2_1,
    .baudrate = IOLINK_BAUDRATE_COM3,
    .min_cycle_time = 20U,
    .pd_in_len = 0U,
    .pd_out_len = 0U,
};

static int reset_fake_phy(void** state)
{
    (void)state;
    g_send_calls = 0;
    g_forced_send_return = INT_MIN;
    memset(g_sent, 0, sizeof(g_sent));
    memset(g_sent_len, 0, sizeof(g_sent_len));
    return 0;
}

static void enter_operate(iolink_master_port_t* port)
{
    uint8_t startup_resp[2] = {0U};
    static const uint8_t operate_ack[1] = {0x2DU};

    assert_int_equal(iolink_master_init(port, &g_fake_phy, &g_config), 0);

    iolink_master_process(port);
    iolink_master_process(port);
    startup_resp[1] = test_ck6_type0(startup_resp[0]);
    assert_int_equal(iolink_master_on_rx(port, startup_resp, sizeof(startup_resp)), 0);
    iolink_master_process(port);

    /* Figure A.5: the DeviceOperate Type-0 WRITE is answered with the CKS octet
       alone (oracle over [0x00] = 0x2D); the port enters OPERATE only after it
       has consumed and verified that reply. */
    assert_int_equal(iolink_master_on_rx(port, operate_ack, sizeof(operate_ack)), 0);

    assert_int_equal(iolink_master_get_state(port), IOLINK_MASTER_STATE_OPERATE);
    assert_int_equal(g_send_calls, 3);
}

/** @brief Copy the fully assembled ISDU request octet stream out of private state. */
static uint16_t copy_request(iolink_master_port_t* port, uint8_t* out, uint16_t out_len)
{
    iolink_master_isdu_state_t* isdu = &iolink_master_port_state(port)->isdu;
    uint16_t n = isdu->request_len;

    assert_true(n <= out_len);
    if(n > 0U)
    {
        memcpy(out, isdu->request, n);
    }
    return n;
}

/** @brief Deliver a complete response octet stream to the ISDU transport. */
static void drain_request(iolink_master_port_t* port)
{
    uint8_t od[IOLINK_ISDU_BUFFER_SIZE] = {0U};
    uint8_t od_len = iolink_master_port_state(port)->od_len;

    while(iolink_master_port_state(port)->isdu.phase == IOLINK_MASTER_ISDU_PHASE_REQUEST)
    {
        iolink_master_isdu_fill_od(port, od, od_len);
    }

    assert_int_equal(iolink_master_port_state(port)->isdu.phase, IOLINK_MASTER_ISDU_PHASE_WAIT);
}

/** @brief Drain the request, then deliver a complete response octet stream. */
static void feed_response_stream(iolink_master_port_t* port, const uint8_t* data, uint16_t len)
{
    uint16_t i;

    drain_request(port);
    for(i = 0U; i < len; i++)
    {
        iolink_master_isdu_on_od(port, &data[i], 1U);
    }
}

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
    iolink_master_port_t port;
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);

    (void)state;

    enter_operate(&port);

    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), 1);
}

static void test_read_isdu_rejects_non_operate_state(void** state)
{
    iolink_master_port_t port = {0};
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);

    (void)state;

    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), -5);
}

static void test_read_isdu_encodes_8bit_index_format(void** state)
{
    iolink_master_port_t port;
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);
    uint8_t stream[8] = {0U};
    static const uint8_t expected[] = {0x93U, 0x10U, 0x83U};

    (void)state;

    enter_operate(&port);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), 1);

    assert_int_equal(copy_request(&port, stream, sizeof(stream)), sizeof(expected));
    assert_memory_equal(stream, expected, sizeof(expected));
}

static void test_read_isdu_encodes_8bit_index_and_subindex(void** state)
{
    iolink_master_port_t port;
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);
    uint8_t stream[8] = {0U};
    static const uint8_t expected[] = {0xA4U, 0x10U, 0x01U, 0xB5U};

    (void)state;

    enter_operate(&port);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0x01U, data, &len), 1);

    assert_int_equal(copy_request(&port, stream, sizeof(stream)), sizeof(expected));
    assert_memory_equal(stream, expected, sizeof(expected));
}

static void test_read_isdu_encodes_16bit_index_format(void** state)
{
    iolink_master_port_t port;
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);
    uint8_t stream[8] = {0U};
    static const uint8_t body[] = {0x12U, 0x34U, 0x56U};

    (void)state;

    enter_operate(&port);
    assert_int_equal(iolink_master_read_isdu(&port, 0x1234U, 0x56U, data, &len), 1);

    assert_int_equal(copy_request(&port, stream, sizeof(stream)), 5U);
    assert_int_equal(stream[0], 0xB5U);
    assert_memory_equal(&stream[1], body, sizeof(body));
    assert_int_equal(stream[4], 0xC5U); /* oracle: A.5.6 XOR of B5 12 34 56 */
}

static void test_read_isdu_request_is_segmented_with_flowctrl(void** state)
{
    iolink_master_port_t port;
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);
    uint8_t od = 0U;
    uint8_t flowctrl = 0U;
    bool read = false;

    (void)state;

    enter_operate(&port);
    assert_int_equal(iolink_master_read_isdu(&port, 0x1234U, 0x56U, data, &len), 1);

    /* First M-sequence: ISDU request, FlowCTRL START (0x10), one OD octet. */
    assert_true(iolink_master_isdu_channel_access(&port, &read, &flowctrl));
    assert_false(read);
    assert_int_equal(flowctrl, IOLINK_FLOWCTRL_START);
    iolink_master_isdu_fill_od(&port, &od, 1U);
    assert_int_equal(od, 0xB5U);

    /* Subsequent M-sequences carry COUNT 1..15 then wrap (Table 52). */
    assert_true(iolink_master_isdu_channel_access(&port, &read, &flowctrl));
    assert_false(read);
    assert_int_equal(flowctrl, 1U);
    iolink_master_isdu_fill_od(&port, &od, 1U);
    assert_int_equal(od, 0x12U);

    iolink_master_isdu_fill_od(&port, &od, 1U);
    assert_int_equal(od, 0x34U);

    iolink_master_isdu_fill_od(&port, &od, 1U);
    assert_int_equal(od, 0x56U);

    iolink_master_isdu_fill_od(&port, &od, 1U);
    assert_int_equal(od, 0xC5U);

    /* Request fully sent: the master now polls with a read + START. */
    assert_true(iolink_master_isdu_channel_access(&port, &read, &flowctrl));
    assert_true(read);
    assert_int_equal(flowctrl, IOLINK_FLOWCTRL_START);
}

static void test_write_isdu_encodes_length_and_chkpdu(void** state)
{
    iolink_master_port_t port;
    const uint8_t data[] = {0x12U, 0x34U};
    uint8_t stream[16] = {0U};
    static const uint8_t expected[] = {0x26U, 0x10U, 0x01U, 0x12U, 0x34U, 0x11U};

    (void)state;

    enter_operate(&port);
    assert_int_equal(iolink_master_write_isdu(&port, 0x0010U, 0x01U, data, sizeof(data)), 1);

    assert_int_equal(copy_request(&port, stream, sizeof(stream)), sizeof(expected));
    assert_memory_equal(stream, expected, sizeof(expected));
}

static void test_write_isdu_uses_extended_length_above_15_octets(void** state)
{
    iolink_master_port_t port;
    uint8_t data[60];
    uint8_t stream[80] = {0U};
    uint16_t stream_len;

    (void)state;

    memset(data, 0xAA, sizeof(data));
    enter_operate(&port);
    /* 8-bit index, no subindex: 1(I-Service/Length) + 1(ExtLength) + 1(index) +
       60(data) + 1(CHKPDU) = 64 (A.5.3 / Figure A.18 ex. 4: n counts ExtLength). */
    assert_int_equal(iolink_master_write_isdu(&port, 0x0010U, 0U, data, sizeof(data)), 1);

    stream_len = copy_request(&port, stream, sizeof(stream));
    assert_int_equal(stream_len, 64U);
    assert_int_equal(stream[0], 0x11U);  /* Write Request, Length = 1 -> ExtLength */
    assert_int_equal(stream[1], 64U);    /* ExtLength = total ISDU octets incl. itself */
    assert_int_equal(stream[2], 0x10U);  /* Index */
    assert_memory_equal(&stream[3], data, sizeof(data));

    {
        uint8_t chk = 0U;
        uint16_t i;

        for(i = 0U; i < (stream_len - 1U); i++)
        {
            chk ^= stream[i];
        }
        assert_int_equal(stream[stream_len - 1U], chk);
    }
}

static void test_read_isdu_completes_from_response_stream(void** state)
{
    iolink_master_port_t port;
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);
    static const uint8_t response[] = {0xD4U, 0x12U, 0x34U, 0xF2U};

    (void)state;

    enter_operate(&port);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), 1);

    feed_response_stream(&port, response, sizeof(response));

    len = sizeof(data);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), 0);
    assert_int_equal(len, 2U);
    assert_int_equal(data[0], 0x12U);
    assert_int_equal(data[1], 0x34U);
}

static void test_read_isdu_polls_while_device_is_busy(void** state)
{
    iolink_master_port_t port;
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);
    static const uint8_t response[] = {0xD4U, 0x12U, 0x34U, 0xF2U};
    uint8_t od = 0U;
    uint8_t flowctrl = 0U;
    bool read = false;

    (void)state;

    enter_operate(&port);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), 1);
    iolink_master_isdu_fill_od(&port, &od, 3U);

    /* Table A.14: 0x00 No Service and 0x01 Busy keep the poll going. */
    iolink_master_isdu_on_od(&port, (const uint8_t[]){0x00U}, 1U);
    iolink_master_isdu_on_od(&port, (const uint8_t[]){0x01U}, 1U);

    assert_true(iolink_master_isdu_channel_access(&port, &read, &flowctrl));
    assert_true(read);
    assert_int_equal(flowctrl, IOLINK_FLOWCTRL_START);

    feed_response_stream(&port, response, sizeof(response));

    len = sizeof(data);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), 0);
    assert_int_equal(len, 2U);
}

static void test_read_isdu_decodes_extended_length_response(void** state)
{
    iolink_master_port_t port;
    uint8_t data[80] = {0U};
    uint8_t len = sizeof(data);
    uint8_t response[80] = {0U};
    uint8_t payload[64];
    uint16_t i;
    uint8_t chk = 0U;

    (void)state;

    for(i = 0U; i < sizeof(payload); i++)
    {
        payload[i] = (uint8_t)i;
    }

    /* Read Response (+), Length = 1, ExtLength = total = 1+1+64+1 = 67 (A.5.3). */
    response[0] = 0xD1U;
    response[1] = 67U;
    memcpy(&response[2], payload, sizeof(payload));
    for(i = 0U; i < 66U; i++)
    {
        chk ^= response[i];
    }
    response[66] = chk;

    enter_operate(&port);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), 1);

    feed_response_stream(&port, response, 67U);

    len = sizeof(data);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), 0);
    assert_int_equal(len, 64U);
    assert_memory_equal(data, payload, sizeof(payload));
}

static void test_read_isdu_reports_negative_response_error_type(void** state)
{
    iolink_master_port_t port;
    iolink_master_diagnostics_t diagnostics;
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);
    static const uint8_t response[] = {0xC4U, 0x80U, 0x11U, 0x55U};

    (void)state;

    enter_operate(&port);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), 1);

    feed_response_stream(&port, response, sizeof(response));

    len = sizeof(data);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), -4);
    assert_int_equal(iolink_master_get_diagnostics(&port, &diagnostics), 0);
    assert_int_equal(diagnostics.last_isdu_error, 0x8011U);
}

static void test_read_isdu_rejects_corrupted_chkpdu_and_aborts(void** state)
{
    iolink_master_port_t port;
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);
    static const uint8_t response[] = {0xD4U, 0x12U, 0x34U, 0xF3U};

    (void)state;

    enter_operate(&port);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), 1);

    feed_response_stream(&port, response, sizeof(response));

    len = sizeof(data);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), -4);

    /* A.5.6 violation: the transport latches an ABORT to emit (Table 53 T11). */
    assert_true(iolink_master_isdu_take_abort(&port));
    assert_false(iolink_master_isdu_take_abort(&port));
}

static void test_read_device_info_reads_direct_parameter_page1(void** state)
{
    static const uint8_t page1[] = {
        0x00U,
        0x00U,
        10U,
        0x01U,
        0x11U,
        0x00U,
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
    iolink_master_port_t port;
    iolink_master_device_info_t info;
    uint8_t response[24] = {0U};
    uint8_t chk = 0U;
    uint16_t i;

    (void)state;

    enter_operate(&port);

    assert_int_equal(iolink_master_read_device_info(&port), 1);

    response[0] = 0xD1U; /* Read Response (+), ExtLength form */
    response[1] = 19U;   /* total = 1 + 1 + 16 + 1 */
    memcpy(&response[2], page1, sizeof(page1));
    for(i = 0U; i < 18U; i++)
    {
        chk ^= response[i];
    }
    response[18] = chk;
    feed_response_stream(&port, response, 19U);

    assert_int_equal(iolink_master_read_device_info(&port), 0);
    assert_int_equal(iolink_master_get_device_info(&port, &info), 0);
    assert_int_equal(info.vendor_id, 0x1234U);
    assert_int_equal(info.device_id, 0x56789AU);
}

static void test_read_isdu_reports_small_result_buffer(void** state)
{
    iolink_master_port_t port;
    uint8_t data[2] = {0U};
    uint8_t len = sizeof(data);
    /* Read Response (+) with 3 data octets: Length = 1+3+1 = 5. */
    static const uint8_t response[] = {0xD5U, 0x11U, 0x22U, 0x33U, 0xD5U};

    (void)state;

    enter_operate(&port);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), 1);

    feed_response_stream(&port, response, sizeof(response));

    len = sizeof(data);
    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), -2);
    assert_int_equal(len, 3U);
}

static void test_write_isdu_rejects_invalid_args(void** state)
{
    iolink_master_port_t port = {0};
    const uint8_t data[] = {0x11U, 0x22U};

    (void)state;

    assert_int_equal(iolink_master_write_isdu(NULL, 0x0010U, 0U, data, sizeof(data)), -1);
    assert_int_equal(iolink_master_write_isdu(&port, 0x0010U, 0U, NULL, sizeof(data)), -1);
}

static void test_write_isdu_completes_after_positive_ack(void** state)
{
    iolink_master_port_t port;
    const uint8_t data[] = {0x11U, 0x22U};
    static const uint8_t response[] = {0x52U, 0x52U};

    (void)state;

    enter_operate(&port);

    assert_int_equal(iolink_master_write_isdu(&port, 0x0010U, 0U, data, sizeof(data)), 1);

    feed_response_stream(&port, response, sizeof(response));
    assert_int_equal(iolink_master_write_isdu(&port, 0x0010U, 0U, data, sizeof(data)), 0);
}

static void test_write_isdu_rejects_non_operate_state(void** state)
{
    iolink_master_port_t port = {0};
    const uint8_t data[] = {0x11U, 0x22U};

    (void)state;

    assert_int_equal(iolink_master_write_isdu(&port, 0x0010U, 0U, data, sizeof(data)), -5);
}

static void test_isdu_rejects_second_request_while_busy(void** state)
{
    iolink_master_port_t port;
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);

    (void)state;

    enter_operate(&port);

    assert_int_equal(iolink_master_read_isdu(&port, 0x0010U, 0U, data, &len), 1);
    assert_int_equal(iolink_master_write_isdu(&port, 0x0018U, 0U, data, 1U), -3);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_read_isdu_rejects_invalid_args, reset_fake_phy),
        cmocka_unit_test_setup(test_read_isdu_returns_pending_for_valid_request, reset_fake_phy),
        cmocka_unit_test_setup(test_read_isdu_rejects_non_operate_state, reset_fake_phy),
        cmocka_unit_test_setup(test_read_isdu_encodes_8bit_index_format, reset_fake_phy),
        cmocka_unit_test_setup(test_read_isdu_encodes_8bit_index_and_subindex, reset_fake_phy),
        cmocka_unit_test_setup(test_read_isdu_encodes_16bit_index_format, reset_fake_phy),
        cmocka_unit_test_setup(test_read_isdu_request_is_segmented_with_flowctrl, reset_fake_phy),
        cmocka_unit_test_setup(test_write_isdu_encodes_length_and_chkpdu, reset_fake_phy),
        cmocka_unit_test_setup(test_write_isdu_uses_extended_length_above_15_octets, reset_fake_phy),
        cmocka_unit_test_setup(test_read_isdu_completes_from_response_stream, reset_fake_phy),
        cmocka_unit_test_setup(test_read_isdu_polls_while_device_is_busy, reset_fake_phy),
        cmocka_unit_test_setup(test_read_isdu_decodes_extended_length_response, reset_fake_phy),
        cmocka_unit_test_setup(test_read_isdu_reports_negative_response_error_type, reset_fake_phy),
        cmocka_unit_test_setup(test_read_isdu_rejects_corrupted_chkpdu_and_aborts, reset_fake_phy),
        cmocka_unit_test_setup(test_read_device_info_reads_direct_parameter_page1, reset_fake_phy),
        cmocka_unit_test_setup(test_read_isdu_reports_small_result_buffer, reset_fake_phy),
        cmocka_unit_test_setup(test_write_isdu_rejects_invalid_args, reset_fake_phy),
        cmocka_unit_test_setup(test_write_isdu_completes_after_positive_ack, reset_fake_phy),
        cmocka_unit_test_setup(test_write_isdu_rejects_non_operate_state, reset_fake_phy),
        cmocka_unit_test_setup(test_isdu_rejects_second_request_while_busy, reset_fake_phy),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
