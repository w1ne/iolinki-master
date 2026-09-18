#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "iolinki/crc.h"
#include "test_wire_helpers.h"
#include "iolinki/protocol.h"
#include "iolinki_master/master.h"
#include "../src/master_internal.h"

static int g_send_calls;
static uint8_t g_sent[64][8];
static size_t g_sent_len[64];

static int fake_send(void* user, const uint8_t* data, size_t len)
{
    (void)user;
    assert_non_null(data);
    assert_in_range(g_send_calls, 0, 63);
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
    frame[1] = test_ck6_type0(frame[0]);
    assert_int_equal(iolink_master_on_rx(port, frame, sizeof(frame)), IOLINK_MASTER_STATUS_OK);
}

static void enter_type0_operate(iolink_master_port_t* port)
{
    static const uint8_t operate_ack[1] = {0x2DU};

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
    /* Figure A.5: consume the CKS-only reply to DeviceOperate before OPERATE. */
    assert_int_equal(iolink_master_on_rx(port, operate_ack, sizeof(operate_ack)),
                     IOLINK_MASTER_STATUS_OK);
    assert_int_equal(iolink_master_get_state(port), IOLINK_MASTER_STATE_OPERATE);
}

/** @brief Drive the port until the ISDU request has been fully transmitted. */
static void drain_request(iolink_master_port_t* port)
{
    uint16_t guard = 0U;

    while (iolink_master_port_state(port)->isdu.phase == IOLINK_MASTER_ISDU_PHASE_REQUEST) {
        assert_int_equal(iolink_master_tick_event(port, IOLINK_MASTER_TICK_CYCLE_DUE),
                         IOLINK_MASTER_STATUS_OK);
        guard++;
        assert_true(guard < 256U);
    }
    assert_int_equal(iolink_master_port_state(port)->isdu.phase, IOLINK_MASTER_ISDU_PHASE_WAIT);
}

/** @brief Deliver a complete ISDU response stream to the transport. */
static void feed_isdu_stream(iolink_master_port_t* port, const uint8_t* data, uint16_t len)
{
    uint16_t i;

    drain_request(port);
    for (i = 0U; i < len; i++) {
        iolink_master_isdu_on_od(port, &data[i], 1U);
    }
}

/** @brief Build a positive Read Response (+) stream carrying @p data and feed it. */
static void feed_read_response(iolink_master_port_t* port, const uint8_t* data, uint8_t len)
{
    uint8_t stream[IOLINK_ISDU_BUFFER_SIZE] = {0U};
    uint16_t total = (uint16_t) (1U + len + 1U);
    uint16_t i;
    uint8_t chk = 0U;

    if (total <= IOLINK_MASTER_ISDU_LENGTH_NIBBLE_MAX) {
        stream[0] = (uint8_t) (0xD0U | (uint8_t) total);
        if (len > 0U) {
            memcpy(&stream[1], data, len);
        }
        for (i = 0U; i < (uint16_t) (len + 1U); i++) {
            chk ^= stream[i];
        }
        stream[(uint16_t) (len + 1U)] = chk;
    }
    else {
        stream[0] = 0xD1U;
        stream[1] = (uint8_t) total;
        if (len > 0U) {
            memcpy(&stream[2], data, len);
        }
        for (i = 0U; i < (uint16_t) (len + 2U); i++) {
            chk ^= stream[i];
        }
        stream[(uint16_t) (len + 2U)] = chk;
    }

    feed_isdu_stream(port, stream, total);
}

static void test_public_type0_isdu_read_completes_without_private_state(void** state)
{
    iolink_master_port_t port;
    uint8_t data[4] = {0U};
    uint8_t len = sizeof(data);
    static const uint8_t payload[] = {0xCAU, 0xFEU};

    (void)state;

    enter_type0_operate(&port);

    assert_int_equal(iolink_master_read_isdu(&port, 0x1234U, 0x56U, data, &len),
                     IOLINK_MASTER_STATUS_PENDING);

    feed_read_response(&port, payload, sizeof(payload));

    assert_int_equal(iolink_master_read_isdu(&port, 0x1234U, 0x56U, data, &len),
                     IOLINK_MASTER_STATUS_OK);
    assert_int_equal(len, 2U);
    assert_int_equal(data[0], 0xCAU);
    assert_int_equal(data[1], 0xFEU);
}

static void test_public_data_storage_read_uses_standard_index(void** state)
{
    iolink_master_port_t port;
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);
    static const uint8_t payload[] = {0xDEU, 0xADU};

    (void)state;

    enter_type0_operate(&port);

    assert_int_equal(iolink_master_read_data_storage(&port, data, &len),
                     IOLINK_MASTER_STATUS_PENDING);
    feed_read_response(&port, payload, sizeof(payload));

    assert_int_equal(iolink_master_read_data_storage(&port, data, &len),
                     IOLINK_MASTER_STATUS_OK);
    assert_int_equal(len, 2U);
    assert_memory_equal(data, payload, sizeof(payload));
}

static void test_public_detailed_device_status_read_uses_standard_index(void** state)
{
    iolink_master_port_t port;
    uint8_t data[8] = {0U};
    uint8_t len = sizeof(data);
    static const uint8_t payload[] = {0xE2U, 0x01U, 0x42U, 0x10U};

    (void)state;

    enter_type0_operate(&port);

    assert_int_equal(iolink_master_read_detailed_device_status(&port, data, &len),
                     IOLINK_MASTER_STATUS_PENDING);
    feed_read_response(&port, payload, sizeof(payload));

    assert_int_equal(iolink_master_read_detailed_device_status(&port, data, &len),
                     IOLINK_MASTER_STATUS_OK);
    assert_memory_equal(data, payload, sizeof(payload));
}

/** @brief Serve one DIAGNOSIS event-memory read request with @p value at @p addr.
 *
 * Drives one tick so the master emits MC = R|DIAGNOSIS|addr, asserts that frame,
 * then injects the A.1.5 TYPE_0 reply `[value, CKS]`.
 */
static void serve_event_memory_read(iolink_master_port_t* port, uint8_t addr, uint8_t value)
{
    assert_int_equal(iolink_master_tick_event(port, IOLINK_MASTER_TICK_CYCLE_DUE),
                     IOLINK_MASTER_STATUS_OK);
    assert_true(g_send_calls > 0);
    assert_int_equal(g_sent[g_send_calls - 1][0], (uint8_t) (0xC0U | addr));
    feed_type0_byte(port, value);
}

static void test_public_event_code_read_uses_diagnosis_event_memory(void** state)
{
    iolink_master_port_t port;
    uint16_t event_code = 0U;
    /* Table 58: StatusCode bit 0 = slot 1 active; slot 1 = E2 42 10. */
    static const uint8_t memory[] = {0x01U, 0xE2U, 0x42U, 0x10U};

    (void) state;

    enter_type0_operate(&port);

    assert_int_equal(iolink_master_read_event_code(&port, &event_code),
                     IOLINK_MASTER_STATUS_PENDING);
    serve_event_memory_read(&port, 0U, memory[0]);
    serve_event_memory_read(&port, 1U, memory[1]);
    serve_event_memory_read(&port, 2U, memory[2]);
    serve_event_memory_read(&port, 3U, memory[3]);

    assert_int_equal(iolink_master_read_event_code(&port, &event_code),
                     IOLINK_MASTER_STATUS_OK);
    assert_int_equal(event_code, 0x4210U);
}

static void test_public_event_ack_reads_and_confirms_status_code(void** state)
{
    iolink_master_port_t port;
    uint16_t event_code = 0U;
    static const uint8_t memory[] = {0x01U, 0xE2U, 0x42U, 0x10U};

    (void) state;

    enter_type0_operate(&port);

    assert_int_equal(iolink_master_ack_event(&port, &event_code), IOLINK_MASTER_STATUS_PENDING);
    serve_event_memory_read(&port, 0U, memory[0]);
    serve_event_memory_read(&port, 1U, memory[1]);
    serve_event_memory_read(&port, 2U, memory[2]);
    serve_event_memory_read(&port, 3U, memory[3]);

    /* Table 59 T8: confirm by writing any value to the StatusCode at address 0. */
    assert_int_equal(iolink_master_tick_event(&port, IOLINK_MASTER_TICK_CYCLE_DUE),
                     IOLINK_MASTER_STATUS_OK);
    assert_int_equal(g_sent[g_send_calls - 1][0], 0x40U);

    assert_int_equal(iolink_master_ack_event(&port, &event_code), IOLINK_MASTER_STATUS_OK);
    assert_int_equal(event_code, 0x4210U);
}

static void test_public_event_details_read_decodes_diagnosis_memory(void** state)
{
    iolink_master_port_t port;
    iolink_master_event_t events[2];
    uint8_t count = 0U;
    static const uint8_t memory[] = {0x01U, 0xE2U, 0x42U, 0x10U};

    (void) state;

    memset(events, 0, sizeof(events));
    enter_type0_operate(&port);

    assert_int_equal(iolink_master_read_event_details(&port, events,
                                                      (uint8_t) (sizeof(events) / sizeof(events[0])),
                                                      &count),
                     IOLINK_MASTER_STATUS_PENDING);
    serve_event_memory_read(&port, 0U, memory[0]);
    serve_event_memory_read(&port, 1U, memory[1]);
    serve_event_memory_read(&port, 2U, memory[2]);
    serve_event_memory_read(&port, 3U, memory[3]);

    assert_int_equal(iolink_master_read_event_details(&port, events,
                                                      (uint8_t) (sizeof(events) / sizeof(events[0])),
                                                      &count),
                     IOLINK_MASTER_STATUS_OK);
    assert_int_equal(count, 1U);
    assert_int_equal(events[0].qualifier, 0xE2U);
    assert_int_equal(events[0].type, IOLINK_MASTER_EVENT_TYPE_WARNING);
    assert_int_equal(events[0].code, 0x4210U);
}

static void test_public_isdu_verify_readback_compares_value(void** state)
{
    iolink_master_port_t port;
    const uint8_t expected[] = {0x12U, 0x34U};
    const uint8_t mismatch[] = {0x12U, 0x35U};
    static const uint8_t payload[] = {0x12U, 0x34U};

    (void)state;

    enter_type0_operate(&port);

    assert_int_equal(iolink_master_verify_isdu(&port, 0x0010U, 0U, expected, sizeof(expected)),
                     IOLINK_MASTER_STATUS_PENDING);
    feed_read_response(&port, payload, sizeof(payload));
    assert_int_equal(iolink_master_verify_isdu(&port, 0x0010U, 0U, expected, sizeof(expected)),
                     IOLINK_MASTER_STATUS_OK);

    assert_int_equal(iolink_master_verify_isdu(&port, 0x0010U, 0U, mismatch, sizeof(mismatch)),
                     IOLINK_MASTER_STATUS_PENDING);
    feed_read_response(&port, payload, sizeof(payload));
    assert_int_equal(iolink_master_verify_isdu(&port, 0x0010U, 0U, mismatch, sizeof(mismatch)),
                     IOLINK_MASTER_ISDU_ERR_VERIFY_FAILED);
}

static void test_public_data_storage_verify_uses_standard_index(void** state)
{
    iolink_master_port_t port;
    const uint8_t expected[] = {0xDEU, 0xADU};
    static const uint8_t payload[] = {0xDEU, 0xADU};

    (void)state;

    enter_type0_operate(&port);

    assert_int_equal(iolink_master_verify_data_storage(&port, expected, sizeof(expected)),
                     IOLINK_MASTER_STATUS_PENDING);
    feed_read_response(&port, payload, sizeof(payload));

    assert_int_equal(iolink_master_verify_data_storage(&port, expected, sizeof(expected)),
                     IOLINK_MASTER_STATUS_OK);
}

static void test_public_parameter_download_helpers_write_system_commands(void** state)
{
    iolink_master_port_t port;
    static const uint8_t ack[] = {0x52U, 0x52U};

    (void)state;

    enter_type0_operate(&port);

    assert_int_equal(iolink_master_begin_parameter_download(&port),
                     IOLINK_MASTER_STATUS_PENDING);
    feed_isdu_stream(&port, ack, sizeof(ack));
    assert_int_equal(iolink_master_begin_parameter_download(&port), IOLINK_MASTER_STATUS_OK);

    assert_int_equal(iolink_master_end_parameter_download(&port), IOLINK_MASTER_STATUS_PENDING);
    feed_isdu_stream(&port, ack, sizeof(ack));
    assert_int_equal(iolink_master_end_parameter_download(&port), IOLINK_MASTER_STATUS_OK);
}

static void test_public_parameter_upload_and_store_helpers_write_system_commands(void** state)
{
    iolink_master_port_t port;
    static const uint8_t ack[] = {0x52U, 0x52U};

    (void)state;

    enter_type0_operate(&port);

    assert_int_equal(iolink_master_begin_parameter_upload(&port), IOLINK_MASTER_STATUS_PENDING);
    feed_isdu_stream(&port, ack, sizeof(ack));
    assert_int_equal(iolink_master_begin_parameter_upload(&port), IOLINK_MASTER_STATUS_OK);

    assert_int_equal(iolink_master_end_parameter_upload(&port), IOLINK_MASTER_STATUS_PENDING);
    feed_isdu_stream(&port, ack, sizeof(ack));
    assert_int_equal(iolink_master_end_parameter_upload(&port), IOLINK_MASTER_STATUS_OK);

    assert_int_equal(iolink_master_store_parameter_download(&port),
                     IOLINK_MASTER_STATUS_PENDING);
    feed_isdu_stream(&port, ack, sizeof(ack));
    assert_int_equal(iolink_master_store_parameter_download(&port), IOLINK_MASTER_STATUS_OK);
}

static void test_public_parameter_block_write_sequences_commands_and_readback(void** state)
{
    iolink_master_port_t port;
    const uint8_t value[] = {0x12U, 0x34U};
    static const uint8_t ack[] = {0x52U, 0x52U};
    static const uint8_t readback[] = {0x12U, 0x34U};

    (void)state;

    enter_type0_operate(&port);

    assert_int_equal(iolink_master_write_parameter_block(&port, 0x0040U, 0x01U, value,
                                                         sizeof(value)),
                     IOLINK_MASTER_STATUS_PENDING);
    feed_isdu_stream(&port, ack, sizeof(ack)); /* ParamDownloadStart */
    assert_int_equal(iolink_master_write_parameter_block(&port, 0x0040U, 0x01U, value,
                                                         sizeof(value)),
                     IOLINK_MASTER_STATUS_PENDING);
    feed_isdu_stream(&port, ack, sizeof(ack)); /* value write */
    assert_int_equal(iolink_master_write_parameter_block(&port, 0x0040U, 0x01U, value,
                                                         sizeof(value)),
                     IOLINK_MASTER_STATUS_PENDING);
    feed_isdu_stream(&port, ack, sizeof(ack)); /* ParamDownloadEnd */
    assert_int_equal(iolink_master_write_parameter_block(&port, 0x0040U, 0x01U, value,
                                                         sizeof(value)),
                     IOLINK_MASTER_STATUS_PENDING);
    feed_read_response(&port, readback, sizeof(readback)); /* readback verify */

    assert_int_equal(iolink_master_write_parameter_block(&port, 0x0040U, 0x01U, value,
                                                         sizeof(value)),
                     IOLINK_MASTER_STATUS_OK);
}

static void test_public_parameter_block_write_reports_readback_mismatch(void** state)
{
    iolink_master_port_t port;
    iolink_master_diagnostics_t diagnostics;
    const uint8_t value[] = {0x12U, 0x34U};
    static const uint8_t ack[] = {0x52U, 0x52U};
    static const uint8_t readback[] = {0x12U, 0x35U};

    (void)state;

    enter_type0_operate(&port);

    assert_int_equal(iolink_master_write_parameter_block(&port, 0x0040U, 0x01U, value,
                                                         sizeof(value)),
                     IOLINK_MASTER_STATUS_PENDING);
    assert_int_equal(iolink_master_write_parameter_block(&port, 0x0040U, 0x02U, value,
                                                         sizeof(value)),
                     IOLINK_MASTER_ISDU_ERR_BUSY);

    feed_isdu_stream(&port, ack, sizeof(ack));
    assert_int_equal(iolink_master_write_parameter_block(&port, 0x0040U, 0x01U, value,
                                                         sizeof(value)),
                     IOLINK_MASTER_STATUS_PENDING);
    feed_isdu_stream(&port, ack, sizeof(ack));
    assert_int_equal(iolink_master_write_parameter_block(&port, 0x0040U, 0x01U, value,
                                                         sizeof(value)),
                     IOLINK_MASTER_STATUS_PENDING);
    feed_isdu_stream(&port, ack, sizeof(ack));
    assert_int_equal(iolink_master_write_parameter_block(&port, 0x0040U, 0x01U, value,
                                                         sizeof(value)),
                     IOLINK_MASTER_STATUS_PENDING);
    feed_read_response(&port, readback, sizeof(readback));

    assert_int_equal(iolink_master_write_parameter_block(&port, 0x0040U, 0x01U, value,
                                                         sizeof(value)),
                     IOLINK_MASTER_ISDU_ERR_VERIFY_FAILED);
    assert_int_equal(iolink_master_get_diagnostics(&port, &diagnostics), 0);
    assert_int_equal(diagnostics.last_service_result, IOLINK_MASTER_ISDU_ERR_VERIFY_FAILED);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_public_type0_isdu_read_completes_without_private_state,
                               reset_fixture),
        cmocka_unit_test_setup(test_public_data_storage_read_uses_standard_index, reset_fixture),
        cmocka_unit_test_setup(test_public_detailed_device_status_read_uses_standard_index,
                               reset_fixture),
        cmocka_unit_test_setup(test_public_event_code_read_uses_diagnosis_event_memory,
                               reset_fixture),
        cmocka_unit_test_setup(test_public_event_ack_reads_and_confirms_status_code, reset_fixture),
        cmocka_unit_test_setup(test_public_event_details_read_decodes_diagnosis_memory,
                               reset_fixture),
        cmocka_unit_test_setup(test_public_isdu_verify_readback_compares_value, reset_fixture),
        cmocka_unit_test_setup(test_public_data_storage_verify_uses_standard_index, reset_fixture),
        cmocka_unit_test_setup(test_public_parameter_download_helpers_write_system_commands,
                               reset_fixture),
        cmocka_unit_test_setup(test_public_parameter_upload_and_store_helpers_write_system_commands,
                               reset_fixture),
        cmocka_unit_test_setup(
            test_public_parameter_block_write_sequences_commands_and_readback, reset_fixture),
        cmocka_unit_test_setup(test_public_parameter_block_write_reports_readback_mismatch,
                               reset_fixture),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
