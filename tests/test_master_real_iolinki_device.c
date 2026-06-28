#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "iolinki/application.h"
#include "iolinki/iolink.h"
#include "iolinki_master/master.h"

#define LINK_QUEUE_CAP 128U
#define MAX_PD_LEN 32U

typedef struct
{
    uint8_t bytes[LINK_QUEUE_CAP];
    uint8_t head;
    uint8_t len;
} link_queue_t;

static link_queue_t g_master_to_device;
static link_queue_t g_device_to_master;
static int g_wakeup_pending;
static uint8_t g_last_pd_input[MAX_PD_LEN];
static uint8_t g_last_pd_input_len;
static uint8_t g_last_pd_output[MAX_PD_LEN];
static uint8_t g_last_pd_output_len;

static void q_reset(link_queue_t* q)
{
    memset(q, 0, sizeof(*q));
}

static int q_push(link_queue_t* q, const uint8_t* data, size_t len)
{
    size_t i;

    if((data == NULL) || ((size_t)q->len + len > LINK_QUEUE_CAP))
    {
        return -1;
    }

    for(i = 0U; i < len; i++)
    {
        uint8_t tail = (uint8_t)((q->head + q->len) % LINK_QUEUE_CAP);
        q->bytes[tail] = data[i];
        q->len++;
    }

    return (int)len;
}

static int q_pop(link_queue_t* q, uint8_t* byte)
{
    if((byte == NULL) || (q->len == 0U))
    {
        return 0;
    }

    *byte = q->bytes[q->head];
    q->head = (uint8_t)((q->head + 1U) % LINK_QUEUE_CAP);
    q->len--;
    return 1;
}

static int master_send(const uint8_t* data, size_t len)
{
    return q_push(&g_master_to_device, data, len);
}

static int master_recv(uint8_t* byte)
{
    return q_pop(&g_device_to_master, byte);
}

static int master_wake_up(void)
{
    g_wakeup_pending = 1;
    return 0;
}

static int checked_set_mode(iolink_phy_mode_t mode)
{
    (void)mode;
    return 0;
}

static int phy_noop(void)
{
    return 0;
}

static int device_send(const uint8_t* data, size_t len)
{
    return q_push(&g_device_to_master, data, len);
}

static int device_recv(uint8_t* byte)
{
    return q_pop(&g_master_to_device, byte);
}

static int device_detect_wakeup(void)
{
    int ret = g_wakeup_pending;
    g_wakeup_pending = 0;
    return ret;
}

static void device_set_mode(iolink_phy_mode_t mode)
{
    (void)mode;
}

static void device_set_baudrate(iolink_baudrate_t baudrate)
{
    (void)baudrate;
}

static void on_device_pd_input(const uint8_t* data, uint8_t len)
{
    assert_true(len <= sizeof(g_last_pd_input));
    memcpy(g_last_pd_input, data, len);
    g_last_pd_input_len = len;
}

static void on_device_pd_output(uint8_t* data, uint8_t len)
{
    assert_true(len <= sizeof(g_last_pd_output));
    memcpy(g_last_pd_output, data, len);
    g_last_pd_output_len = len;
}

static void fill_incrementing(uint8_t* data, uint8_t len, uint8_t first)
{
    uint8_t i;

    for(i = 0U; i < len; i++)
    {
        data[i] = (uint8_t)(first + i);
    }
}

static void pump_device(const uint8_t* pd, uint8_t len)
{
    uint8_t i;

    assert_int_equal(iolink_pd_input_update(pd, len, true), 0);
    for(i = 0U; i < 4U; i++)
    {
        iolink_process();
    }
}

static iolink_m_seq_type_t device_mseq_for_master(iolink_master_m_seq_type_t type)
{
    switch(type)
    {
    case IOLINK_MASTER_M_SEQ_TYPE_1_1:
        return IOLINK_M_SEQ_TYPE_1_1;
    case IOLINK_MASTER_M_SEQ_TYPE_1_2:
        return IOLINK_M_SEQ_TYPE_1_2;
    case IOLINK_MASTER_M_SEQ_TYPE_1_V:
        return IOLINK_M_SEQ_TYPE_1_V;
    case IOLINK_MASTER_M_SEQ_TYPE_2_1:
        return IOLINK_M_SEQ_TYPE_2_1;
    case IOLINK_MASTER_M_SEQ_TYPE_2_2:
        return IOLINK_M_SEQ_TYPE_2_2;
    case IOLINK_MASTER_M_SEQ_TYPE_2_V:
        return IOLINK_M_SEQ_TYPE_2_V;
    case IOLINK_MASTER_M_SEQ_TYPE_0:
    default:
        return IOLINK_M_SEQ_TYPE_0;
    }
}

static void assert_bytes_equal(const uint8_t* actual,
                               const uint8_t* expected,
                               uint8_t len)
{
    uint8_t i;

    for(i = 0U; i < len; i++)
    {
        assert_int_equal(actual[i], expected[i]);
    }
}

static void assert_master_real_stack_profile(iolink_master_m_seq_type_t m_seq_type,
                                             uint8_t pd_in_len,
                                             uint8_t pd_out_len,
                                             uint8_t pd_value,
                                             const char* case_name)
{
    static const iolink_phy_api_t master_phy = {
        .send = master_send,
        .recv_byte = master_recv,
    };
    static const iolink_phy_api_t device_phy = {
        .init = phy_noop,
        .set_mode = device_set_mode,
        .set_baudrate = device_set_baudrate,
        .send = device_send,
        .recv_byte = device_recv,
        .detect_wakeup = device_detect_wakeup,
    };
    static const iolink_app_callbacks_t app_callbacks = {
        .on_pd_input = on_device_pd_input,
        .on_pd_output = on_device_pd_output,
    };
    iolink_master_port_t master;
    iolink_master_config_t master_config = {
        .port_mode = IOLINK_MASTER_PORT_MODE_IOLINK,
        .m_seq_type = m_seq_type,
        .baudrate = IOLINK_BAUDRATE_COM2,
        .min_cycle_time = 1U,
        .pd_in_len = pd_in_len,
        .pd_out_len = pd_out_len,
        .response_timeout_100us = 20U,
        .set_mode_checked = checked_set_mode,
        .prepare_tx = phy_noop,
        .prepare_rx = phy_noop,
        .wake_up = master_wake_up,
    };
    iolink_config_t device_config = {
        .m_seq_type = device_mseq_for_master(m_seq_type),
        .min_cycle_time = 1U,
        .pd_in_len = pd_in_len,
        .pd_out_len = pd_out_len,
        .t_pd_us = 0U,
    };
    uint8_t pd_in[MAX_PD_LEN] = {0U};
    uint8_t pd_out[MAX_PD_LEN] = {0U};
    uint8_t pd_len = 0U;
    uint8_t device_pd[MAX_PD_LEN] = {0U};
    uint8_t i;

    q_reset(&g_master_to_device);
    q_reset(&g_device_to_master);
    g_wakeup_pending = 0;
    g_last_pd_input_len = 0U;
    g_last_pd_output_len = 0U;
    memset(g_last_pd_input, 0, sizeof(g_last_pd_input));
    memset(g_last_pd_output, 0, sizeof(g_last_pd_output));
    fill_incrementing(device_pd, pd_in_len, pd_value);
    fill_incrementing(pd_out, pd_out_len, (uint8_t)(pd_value ^ 0x55U));

    iolink_app_register(&app_callbacks);
    assert_int_equal(iolink_init(&device_phy, &device_config), 0);
    iolink_set_timing_enforcement(false);
    assert_int_equal(iolink_master_init(&master, &master_phy, &master_config), 0);
    assert_int_equal(iolink_master_set_pd_out(&master, pd_out, pd_out_len), 0);

    for(i = 0U; i < 20U; i++)
    {
        assert_int_equal(iolink_master_tick_at(&master, IOLINK_MASTER_TICK_CYCLE_DUE, i), 0);
        pump_device(device_pd, pd_in_len);
        (void)iolink_master_tick_at(&master, IOLINK_MASTER_TICK_NONE, i + 1U);

        if(iolink_master_get_state(&master) == IOLINK_MASTER_STATE_OPERATE)
        {
            assert_int_equal(iolink_master_tick_at(&master, IOLINK_MASTER_TICK_CYCLE_DUE, i + 40U), 0);
            pump_device(device_pd, pd_in_len);
            assert_int_equal(
                iolink_master_tick_at(&master, IOLINK_MASTER_TICK_NONE, i + 41U), 1);
            assert_int_equal(iolink_master_get_pd_in(&master, pd_in, sizeof(pd_in), &pd_len), 0);
            assert_int_equal(pd_len, pd_in_len);
            assert_bytes_equal(pd_in, device_pd, pd_in_len);
            assert_int_equal(g_last_pd_input_len, pd_in_len);
            assert_bytes_equal(g_last_pd_input, device_pd, pd_in_len);
            assert_int_equal(g_last_pd_output_len, pd_out_len);
            assert_bytes_equal(g_last_pd_output, pd_out, pd_out_len);
            return;
        }
    }

    fail_msg("real iolinki device stack did not reach OPERATE for %s", case_name);
}

static void test_master_conformance_matrix_with_real_iolinki_device_stack(void** state)
{
    static const struct
    {
        iolink_master_m_seq_type_t m_seq_type;
        uint8_t pd_in_len;
        uint8_t pd_out_len;
        uint8_t pd_value;
    } cases[] = {
        {IOLINK_MASTER_M_SEQ_TYPE_1_1, 1U, 0U, 0x11U},
        {IOLINK_MASTER_M_SEQ_TYPE_1_2, 2U, 1U, 0x22U},
        {IOLINK_MASTER_M_SEQ_TYPE_2_1, 2U, 2U, 0x33U},
        {IOLINK_MASTER_M_SEQ_TYPE_2_2, 3U, 2U, 0x44U},
        {IOLINK_MASTER_M_SEQ_TYPE_1_V, 4U, 1U, 0x55U},
        {IOLINK_MASTER_M_SEQ_TYPE_2_V, 4U, 3U, 0x66U},
    };
    size_t i;

    (void)state;

    for(i = 0U; i < (sizeof(cases) / sizeof(cases[0])); i++)
    {
        print_message("real-stack profile case %zu\n", i);
        assert_master_real_stack_profile(cases[i].m_seq_type,
                                         cases[i].pd_in_len,
                                         cases[i].pd_out_len,
                                         cases[i].pd_value,
                                         "profile matrix");
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_master_conformance_matrix_with_real_iolinki_device_stack),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
