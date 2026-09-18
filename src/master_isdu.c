/**
 * @file master_isdu.c
 * @brief ISDU acyclic service engine plus Data Storage, event, and parameter
 *        block (up/download) services layered on top of it.
 * @ingroup iolinki_master
 *
 * Drives segmented ISDU request/response exchange over the OD channel, exposes
 * read/write ISDU operations, and builds the higher-level Data Storage,
 * detailed-device-status/event, and multi-step parameter-block transfer
 * services on top of the ISDU state machine.
 */

#include "master_internal.h"

#include "iolinki/protocol.h"

#include <string.h>

/** @brief Return true while an ISDU operation is in progress and not yet completed. */
static bool iolink_master_isdu_busy(const iolink_master_port_t* port)
{
    const iolink_master_port_state_t* state = iolink_master_port_const_state(port);

    return (state->isdu.op != IOLINK_MASTER_ISDU_OP_NONE) && !state->isdu.done;
}

/** @brief Return true if the active ISDU op/index/subindex match the given request identity. */
static bool iolink_master_isdu_matches(const iolink_master_port_t* port, iolink_master_isdu_op_t op,
                                       uint16_t index, uint8_t subindex)
{
    const iolink_master_port_state_t* state = iolink_master_port_const_state(port);

    return (state->isdu.op == op) && (state->isdu.index == index) &&
           (state->isdu.subindex == subindex);
}

/** @brief Reset the ISDU state to idle, clearing request/response progress and error. */
static void iolink_master_isdu_clear(iolink_master_port_t* port)
{
    iolink_master_port_state(port)->isdu.op = IOLINK_MASTER_ISDU_OP_NONE;
    iolink_master_port_state(port)->isdu.phase = IOLINK_MASTER_ISDU_PHASE_NONE;
    iolink_master_port_state(port)->isdu.index = 0U;
    iolink_master_port_state(port)->isdu.subindex = 0U;
    iolink_master_port_state(port)->isdu.request_len = 0U;
    iolink_master_port_state(port)->isdu.request_pos = 0U;
    iolink_master_port_state(port)->isdu.flowctrl = IOLINK_FLOWCTRL_IDLE;
    iolink_master_port_state(port)->isdu.chk = 0U;
    iolink_master_port_state(port)->isdu.expected_len = 0U;
    iolink_master_port_state(port)->isdu.response_len = 0U;
    iolink_master_port_state(port)->isdu.response_pos = 0U;
    iolink_master_port_state(port)->isdu.done = false;
    iolink_master_port_state(port)->isdu.idle_pending = false;
    iolink_master_port_state(port)->isdu.abort_pending = false;
    iolink_master_port_state(port)->isdu.error = IOLINK_ISDU_ERROR_NONE;
}

/** @brief Begin a new ISDU operation, initializing the transport state machine. */
static void iolink_master_isdu_start(iolink_master_port_t* port, iolink_master_isdu_op_t op,
                                     uint16_t index, uint8_t subindex)
{
    iolink_master_port_state(port)->isdu.op = op;
    iolink_master_port_state(port)->isdu.phase = IOLINK_MASTER_ISDU_PHASE_REQUEST;
    iolink_master_port_state(port)->isdu.index = index;
    iolink_master_port_state(port)->isdu.subindex = subindex;
    iolink_master_port_state(port)->isdu.request_pos = 0U;
    iolink_master_port_state(port)->isdu.flowctrl = IOLINK_FLOWCTRL_START;
    iolink_master_port_state(port)->isdu.chk = 0U;
    iolink_master_port_state(port)->isdu.expected_len = 0U;
    iolink_master_port_state(port)->isdu.response_len = 0U;
    iolink_master_port_state(port)->isdu.response_pos = 0U;
    iolink_master_port_state(port)->isdu.done = false;
    iolink_master_port_state(port)->isdu.idle_pending = false;
    iolink_master_port_state(port)->isdu.abort_pending = false;
    iolink_master_port_state(port)->isdu.error = IOLINK_ISDU_ERROR_NONE;
}

/** @brief Latch a non-pending service result into diagnostics and pass it through unchanged. */
static int iolink_master_service_result(iolink_master_port_t* port, int ret)
{
    if (ret != IOLINK_MASTER_STATUS_PENDING) {
        iolink_master_port_state(port)->diagnostics.last_service_result = ret;
    }

    return ret;
}

/** @brief Finalize a completed read ISDU: check for device errors, copy the response, and clear
 * state. */
static int iolink_master_isdu_finish_read(iolink_master_port_t* port, uint8_t* data, uint8_t* len)
{
    uint16_t result_len = iolink_master_port_state(port)->isdu.response_len;

    if (iolink_master_port_state(port)->isdu.error != IOLINK_ISDU_ERROR_NONE) {
        bool aborted = iolink_master_port_state(port)->isdu.abort_pending;

        iolink_master_port_state(port)->diagnostics.last_isdu_error =
            iolink_master_port_state(port)->isdu.error;
        iolink_master_isdu_clear(port);
        iolink_master_port_state(port)->isdu.abort_pending = aborted;
        return iolink_master_service_result(port, IOLINK_MASTER_ISDU_ERR_DEVICE);
    }

    if (*len < result_len) {
        *len = (result_len > UINT8_MAX) ? UINT8_MAX : (uint8_t) result_len;
        return iolink_master_service_result(port, IOLINK_MASTER_ISDU_ERR_BUFFER_TOO_SMALL);
    }

    if (result_len > 0U) {
        (void) memcpy(data, iolink_master_port_state(port)->isdu.response, result_len);
    }
    /* Guarded above by `*len < result_len`, so result_len fits in the uint8 out-length. */
    *len = (uint8_t) result_len;
    iolink_master_isdu_clear(port);
    return iolink_master_service_result(port, IOLINK_MASTER_STATUS_OK);
}

/** @brief Finalize a completed write ISDU: check for device errors and clear state. */
static int iolink_master_isdu_finish_write(iolink_master_port_t* port)
{
    if (iolink_master_port_state(port)->isdu.error != IOLINK_ISDU_ERROR_NONE) {
        bool aborted = iolink_master_port_state(port)->isdu.abort_pending;

        iolink_master_port_state(port)->diagnostics.last_isdu_error =
            iolink_master_port_state(port)->isdu.error;
        iolink_master_isdu_clear(port);
        iolink_master_port_state(port)->isdu.abort_pending = aborted;
        return iolink_master_service_result(port, IOLINK_MASTER_ISDU_ERR_DEVICE);
    }

    iolink_master_isdu_clear(port);
    return iolink_master_service_result(port, IOLINK_MASTER_STATUS_OK);
}

/** @brief XOR checksum over an ISDU octet stream with CHKPDU still zero (A.5.6). */
static uint8_t iolink_master_isdu_chkpdu(const uint8_t* data, uint16_t len)
{
    uint8_t chk = 0U;
    uint16_t i;

    for (i = 0U; i < len; i++) {
        chk ^= data[i];
    }

    return chk;
}

/** @brief Number of Index/Subindex octets for the ISDU index format (Table A.15).
 *
 * Returns 1 for the 8-bit Index format, 2 for 8-bit Index + Subindex and 3 for
 * 16-bit Index + Subindex.
 */
static uint8_t iolink_master_isdu_index_len(uint16_t index, uint8_t subindex)
{
    if (index <= 0xFFU) {
        return (subindex == 0U) ? 1U : 2U;
    }

    return 3U;
}

/** @brief Read/write I-Service nibble for an index format (Table A.12, Table A.15).
 *
 * @p read selects the Read Request (0x9/0xA/0xB) or Write Request (0x1/0x2/0x3)
 * service family; @p index_len is 1, 2 or 3 as returned by
 * ::iolink_master_isdu_index_len.
 */
static uint8_t iolink_master_isdu_service(bool read, uint8_t index_len)
{
    if (read) {
        return (uint8_t) (0x08U + index_len);
    }

    return (uint8_t) index_len;
}

void iolink_master_isdu_fill_od(iolink_master_port_t* port, uint8_t* od, uint8_t od_len)
{
    iolink_master_isdu_state_t* isdu;
    uint16_t remaining;
    uint16_t n;
    uint16_t i;

    if ((port == NULL) || (od == NULL)) {
        return;
    }

    (void) memset(od, 0, od_len);
    isdu = &iolink_master_port_state(port)->isdu;

    if (isdu->op == IOLINK_MASTER_ISDU_OP_NONE) {
        return;
    }

    if (isdu->phase == IOLINK_MASTER_ISDU_PHASE_REQUEST) {
        remaining = (uint16_t) (isdu->request_len - isdu->request_pos);
        n = (remaining < od_len) ? remaining : od_len;
        for (i = 0U; i < n; i++) {
            od[i] = isdu->request[(uint16_t) (isdu->request_pos + i)];
        }
        isdu->request_pos = (uint16_t) (isdu->request_pos + n);

        if (isdu->request_pos >= isdu->request_len) {
            /* Request fully sent: switch to reading the device response (T4/T5). */
            isdu->phase = IOLINK_MASTER_ISDU_PHASE_WAIT;
            isdu->flowctrl = IOLINK_FLOWCTRL_START;
        }
        else if (isdu->flowctrl == IOLINK_FLOWCTRL_START) {
            isdu->flowctrl = 1U;
        }
        else {
            /* COUNT increments 1..15 then wraps to 0 (Table 52). */
            isdu->flowctrl = (uint8_t) ((isdu->flowctrl + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
        }
    }
    else if (isdu->phase == IOLINK_MASTER_ISDU_PHASE_WAIT) {
        /* T5: keep polling with FlowCTRL START until the response starts. */
        isdu->flowctrl = IOLINK_FLOWCTRL_START;
    }
    else if (isdu->phase == IOLINK_MASTER_ISDU_PHASE_RESPONSE) {
        /* T7: read the remaining response octets with COUNT, starting at 1. */
        if ((isdu->flowctrl == IOLINK_FLOWCTRL_START) ||
            (isdu->flowctrl == IOLINK_FLOWCTRL_IDLE)) {
            isdu->flowctrl = 1U;
        }
        else {
            isdu->flowctrl = (uint8_t) ((isdu->flowctrl + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
        }
    }
    else {
        /* Idle: no ISDU data to transmit. */
    }
}

/** @brief Validate and decode a complete ISDU response stream (A.5.6, Table A.13).
 *
 * Verifies the CHKPDU (XOR of every octet, CHKPDU included, must be zero), maps
 * a negative response (I-Service 0x4/0xC) to its 16-bit ErrorType and compacts a
 * positive response to its Data octets in @c isdu.response.
 */
static void iolink_master_isdu_decode_response(iolink_master_port_t* port)
{
    iolink_master_isdu_state_t* isdu = &iolink_master_port_state(port)->isdu;
    uint8_t service;
    uint16_t payload_start;
    uint16_t payload_len;

    if (iolink_master_isdu_chkpdu(isdu->response, isdu->response_len) != 0U) {
        /* A.5.6: a non-zero XOR means the PDU is perturbed; abort the service. */
        isdu->error = IOLINK_ISDU_ERROR_SEGMENTATION;
        isdu->done = true;
        isdu->abort_pending = true;
        isdu->phase = IOLINK_MASTER_ISDU_PHASE_NONE;
        return;
    }

    service = (uint8_t) (isdu->response[0] >> IOLINK_MASTER_ISDU_SERVICE_SHIFT);

    if ((service == 0x4U) || (service == 0xCU)) {
        /* Table A.13: negative responses carry ErrorType = ErrorCode, AdditionalCode. */
        if (isdu->response_len >= 4U) {
            isdu->error =
                (uint16_t) (((uint16_t) isdu->response[1] << 8U) | isdu->response[2]);
        }
        else {
            isdu->error = IOLINK_ISDU_ERROR_SEGMENTATION;
        }
        isdu->done = true;
        isdu->idle_pending = true;
        isdu->phase = IOLINK_MASTER_ISDU_PHASE_NONE;
        return;
    }

    /* Positive response: [I-Service][ExtLength if Length==1][Data...][CHKPDU]. */
    payload_start = ((isdu->response[0] & 0x0FU) == 0x01U) ? 2U : 1U;
    payload_len = (uint16_t) (isdu->response_len - payload_start - IOLINK_MASTER_ISDU_CHKPDU_LEN);
    if (payload_len > 0U) {
        (void) memmove(isdu->response, &isdu->response[payload_start], payload_len);
    }
    isdu->response_len = payload_len;
    isdu->done = true;
    isdu->idle_pending = true;
    isdu->phase = IOLINK_MASTER_ISDU_PHASE_NONE;
}

void iolink_master_isdu_on_od(iolink_master_port_t* port, const uint8_t* od, uint8_t od_len)
{
    iolink_master_isdu_state_t* isdu;
    uint16_t i;
    uint8_t byte;

    if ((port == NULL) || (od == NULL)) {
        return;
    }

    isdu = &iolink_master_port_state(port)->isdu;

    if ((isdu->op == IOLINK_MASTER_ISDU_OP_NONE) || isdu->done ||
        (isdu->phase == IOLINK_MASTER_ISDU_PHASE_REQUEST) ||
        (isdu->phase == IOLINK_MASTER_ISDU_PHASE_NONE)) {
        return;
    }

    for (i = 0U; i < od_len; i++) {
        byte = od[i];

        if (isdu->phase == IOLINK_MASTER_ISDU_PHASE_WAIT) {
            /* Table A.12/A.14: 0x00 = no service (still waiting), 0x01 = busy. */
            if ((byte == 0x00U) || (byte == 0x01U)) {
                continue;
            }
            isdu->phase = IOLINK_MASTER_ISDU_PHASE_RESPONSE;
            isdu->response_len = 0U;
            isdu->response_pos = 0U;
            isdu->expected_len = 0U;
            /* Table 52: the octets in this reply were fetched by START; the next
               read must carry COUNT 1. Leaving START in place re-polls the
               device, which repeats the same octets (7.3.6.2) and the assembled
               response duplicates its first segment. */
            isdu->flowctrl = 1U;
        }

        if (isdu->response_len >= IOLINK_ISDU_BUFFER_SIZE) {
            isdu->error = IOLINK_ISDU_ERROR_SEGMENTATION;
            isdu->done = true;
            isdu->phase = IOLINK_MASTER_ISDU_PHASE_NONE;
            return;
        }

        isdu->response[isdu->response_len] = byte;
        isdu->response_len++;

        if (isdu->expected_len == 0U) {
            uint8_t len_nibble = (uint8_t) (isdu->response[0] & 0x0FU);

            if (len_nibble >= 2U) {
                isdu->expected_len = len_nibble;
            }
            else if (len_nibble == 1U) {
                if (isdu->response_len < 2U) {
                    continue; /* Need ExtLength to know the total length (A.5.3). */
                }
                isdu->expected_len = isdu->response[1];
            }
            else {
                /* Length 0 is only a protocol "no service" octet, handled above. */
                isdu->error = IOLINK_ISDU_ERROR_SEGMENTATION;
                isdu->done = true;
                isdu->phase = IOLINK_MASTER_ISDU_PHASE_NONE;
                return;
            }
        }

        if (isdu->response_len >= isdu->expected_len) {
            iolink_master_isdu_decode_response(port);
            return;
        }
    }
}

bool iolink_master_isdu_channel_access(const iolink_master_port_t* port, bool* read,
                                       uint8_t* flowctrl)
{
    const iolink_master_isdu_state_t* isdu;

    if (port == NULL) {
        return false;
    }

    isdu = &iolink_master_port_const_state(port)->isdu;
    if (isdu->op == IOLINK_MASTER_ISDU_OP_NONE) {
        return false;
    }

    if (isdu->done && !isdu->idle_pending) {
        /* Service complete and the T8 IDLE frame already sent: nothing to do. */
        return false;
    }

    if (isdu->done) {
        /* Table 53 T8: conclude the service with an ISDU read, FlowCTRL = IDLE. */
        if (read != NULL) {
            *read = true;
        }
        if (flowctrl != NULL) {
            *flowctrl = IOLINK_FLOWCTRL_IDLE;
        }
        return true;
    }

    if (read != NULL) {
        *read = (isdu->phase != IOLINK_MASTER_ISDU_PHASE_REQUEST);
    }
    if (flowctrl != NULL) {
        *flowctrl = isdu->flowctrl;
    }

    return true;
}

bool iolink_master_isdu_take_idle(iolink_master_port_t* port)
{
    iolink_master_isdu_state_t* isdu;

    if (port == NULL) {
        return false;
    }

    isdu = &iolink_master_port_state(port)->isdu;
    if (!isdu->idle_pending) {
        return false;
    }

    isdu->idle_pending = false;
    return true;
}

bool iolink_master_isdu_take_abort(iolink_master_port_t* port)
{
    iolink_master_isdu_state_t* isdu;

    if (port == NULL) {
        return false;
    }

    isdu = &iolink_master_port_state(port)->isdu;
    if (!isdu->abort_pending) {
        return false;
    }

    isdu->abort_pending = false;
    return true;
}

int iolink_master_read_isdu(iolink_master_port_t* port, uint16_t index, uint8_t subindex,
                            uint8_t* data, uint8_t* len)
{
    if ((port == NULL) || (data == NULL) || (len == NULL)) {
        return IOLINK_MASTER_ERR_INVALID_ARG;
    }

    if ((iolink_master_port_state(port)->state != IOLINK_MASTER_STATE_OPERATE) &&
        (iolink_master_port_state(port)->state != IOLINK_MASTER_STATE_PREOPERATE)) {
        return IOLINK_MASTER_ISDU_ERR_INVALID_STATE;
    }

    if (iolink_master_isdu_busy(port)) {
        if (iolink_master_isdu_matches(port, IOLINK_MASTER_ISDU_OP_READ, index, subindex)) {
            return IOLINK_MASTER_STATUS_PENDING;
        }
        return IOLINK_MASTER_ISDU_ERR_BUSY;
    }

    if (iolink_master_port_state(port)->isdu.done) {
        if (!iolink_master_isdu_matches(port, IOLINK_MASTER_ISDU_OP_READ, index, subindex)) {
            return IOLINK_MASTER_ISDU_ERR_BUSY;
        }
        return iolink_master_isdu_finish_read(port, data, len);
    }

    {
        iolink_master_isdu_state_t* isdu = &iolink_master_port_state(port)->isdu;
        uint8_t idx_len = iolink_master_isdu_index_len(index, subindex);
        uint8_t total = (uint8_t) (1U + idx_len + IOLINK_MASTER_ISDU_CHKPDU_LEN);

        iolink_master_isdu_start(port, IOLINK_MASTER_ISDU_OP_READ, index, subindex);
        /* Table A.13/A.15: Read Request {I-Service, Length, Index[, Index], [Subindex], CHKPDU}. */
        isdu->request[0] = (uint8_t) ((iolink_master_isdu_service(true, idx_len)
                                       << IOLINK_MASTER_ISDU_SERVICE_SHIFT) |
                                      (total & IOLINK_MASTER_ISDU_LENGTH_NIBBLE_MAX));
        if (idx_len == 3U) {
            isdu->request[1] = (uint8_t) (index >> 8U);
            isdu->request[2] = (uint8_t) (index & 0xFFU);
            isdu->request[3] = subindex;
        }
        else if (idx_len == 2U) {
            isdu->request[1] = (uint8_t) (index & 0xFFU);
            isdu->request[2] = subindex;
        }
        else {
            isdu->request[1] = (uint8_t) (index & 0xFFU);
        }
        isdu->request[total - 1U] = iolink_master_isdu_chkpdu(isdu->request, (uint16_t) (total - 1U));
        isdu->request_len = total;
    }

    return IOLINK_MASTER_STATUS_PENDING;
}

int iolink_master_read_device_info(iolink_master_port_t* port)
{
    uint8_t page[IOLINK_MASTER_DPP1_LEN];
    uint8_t len = sizeof(page);
    int ret;

    if (port == NULL) {
        return IOLINK_MASTER_ERR_INVALID_ARG;
    }

    ret = iolink_master_read_isdu(port, IOLINK_IDX_DIRECT_PARAMETERS_1, 0U, page, &len);
    if (ret != 0) {
        return ret;
    }

    ret = iolink_master_apply_direct_parameter_page1(port, page, len);
    if (ret != 0) {
        return ret;
    }

    return iolink_master_validate_device_info(port);
}

int iolink_master_write_isdu(iolink_master_port_t* port, uint16_t index, uint8_t subindex,
                             const uint8_t* data, uint8_t len)
{
    uint8_t pos = 0U;

    if ((port == NULL) || ((data == NULL) && (len > 0U))) {
        return IOLINK_MASTER_ERR_INVALID_ARG;
    }

    if ((iolink_master_port_state(port)->state != IOLINK_MASTER_STATE_OPERATE) &&
        (iolink_master_port_state(port)->state != IOLINK_MASTER_STATE_PREOPERATE)) {
        return IOLINK_MASTER_ISDU_ERR_INVALID_STATE;
    }

    if (iolink_master_isdu_busy(port)) {
        if (iolink_master_isdu_matches(port, IOLINK_MASTER_ISDU_OP_WRITE, index, subindex)) {
            return IOLINK_MASTER_STATUS_PENDING;
        }
        return IOLINK_MASTER_ISDU_ERR_BUSY;
    }

    if (iolink_master_port_state(port)->isdu.done) {
        if (!iolink_master_isdu_matches(port, IOLINK_MASTER_ISDU_OP_WRITE, index, subindex)) {
            return IOLINK_MASTER_ISDU_ERR_BUSY;
        }
        return iolink_master_isdu_finish_write(port);
    }

    {
        iolink_master_isdu_state_t* isdu = &iolink_master_port_state(port)->isdu;
        uint8_t idx_len = iolink_master_isdu_index_len(index, subindex);
        uint16_t total =
            (uint16_t) (1U + idx_len + len + IOLINK_MASTER_ISDU_CHKPDU_LEN);

        /* A.5.3: 2..15 direct, 17..238 with ExtLength; 16 and >238 are reserved. */
        if ((len > (uint8_t) (IOLINK_ISDU_BUFFER_SIZE - IOLINK_MASTER_ISDU_WRITE_HEADER_MAX)) ||
            (((total > 15U) ? (total + 1U) : total) > IOLINK_MASTER_ISDU_EXT_MAX) ||
            (total == 16U)) {
            return IOLINK_MASTER_ISDU_ERR_BUFFER_TOO_SMALL;
        }

        iolink_master_isdu_start(port, IOLINK_MASTER_ISDU_OP_WRITE, index, subindex);
        /* Table A.13: Write Request {I-Service, LEN, Index[, Index], [Subindex], Data*, CHKPDU}. */
        if (total <= IOLINK_MASTER_ISDU_LENGTH_NIBBLE_MAX) {
            isdu->request[pos++] =
                (uint8_t) ((iolink_master_isdu_service(false, idx_len)
                            << IOLINK_MASTER_ISDU_SERVICE_SHIFT) |
                           (uint8_t) (total & IOLINK_MASTER_ISDU_LENGTH_NIBBLE_MAX));
        }
        else {
            isdu->request[pos++] =
                (uint8_t) ((iolink_master_isdu_service(false, idx_len)
                            << IOLINK_MASTER_ISDU_SERVICE_SHIFT) |
                           IOLINK_MASTER_ISDU_LENGTH_EXTENDED);
            /* A.5.3 / Figure A.18 ex. 4: ExtLength counts the ExtLength octet too. */
            isdu->request[pos++] = (uint8_t) (total + 1U);
        }

        if (idx_len == 3U) {
            isdu->request[pos++] = (uint8_t) (index >> 8U);
            isdu->request[pos++] = (uint8_t) (index & 0xFFU);
            isdu->request[pos++] = subindex;
        }
        else if (idx_len == 2U) {
            isdu->request[pos++] = (uint8_t) (index & 0xFFU);
            isdu->request[pos++] = subindex;
        }
        else {
            isdu->request[pos++] = (uint8_t) (index & 0xFFU);
        }

        if (len > 0U) {
            (void) memcpy(&isdu->request[pos], data, len);
            pos = (uint8_t) (pos + len);
        }

        isdu->request[pos] = iolink_master_isdu_chkpdu(isdu->request, pos);
        pos = (uint8_t) (pos + IOLINK_MASTER_ISDU_CHKPDU_LEN);
        isdu->request_len = pos;
    }

    return IOLINK_MASTER_STATUS_PENDING;
}

int iolink_master_read_data_storage(iolink_master_port_t* port, uint8_t* data, uint8_t* len)
{
    return iolink_master_read_isdu(port, IOLINK_IDX_DATA_STORAGE, 0U, data, len);
}

int iolink_master_write_data_storage(iolink_master_port_t* port, const uint8_t* data, uint8_t len)
{
    return iolink_master_write_isdu(port, IOLINK_IDX_DATA_STORAGE, 0U, data, len);
}

int iolink_master_restore_data_storage(iolink_master_port_t* port, const uint8_t* data, uint8_t len)
{
    return iolink_master_write_parameter_block(port, IOLINK_IDX_DATA_STORAGE, 0U, data, len);
}

int iolink_master_verify_isdu(iolink_master_port_t* port, uint16_t index, uint8_t subindex,
                              const uint8_t* expected, uint8_t len)
{
    uint8_t data[IOLINK_ISDU_BUFFER_SIZE];
    uint8_t read_len = UINT8_MAX;
    int ret;

    if ((expected == NULL) && (len > 0U)) {
        return IOLINK_MASTER_ERR_INVALID_ARG;
    }

    ret = iolink_master_read_isdu(port, index, subindex, data, &read_len);
    if (ret != IOLINK_MASTER_STATUS_OK) {
        return ret;
    }

    if ((read_len != len) || ((len > 0U) && (memcmp(data, expected, len) != 0))) {
        return iolink_master_service_result(port, IOLINK_MASTER_ISDU_ERR_VERIFY_FAILED);
    }

    return iolink_master_service_result(port, IOLINK_MASTER_STATUS_OK);
}

/** @brief Parse the next Data Storage record at *pos, returning its span and advancing the cursor.
 */
static bool iolink_master_ds_next_record(const uint8_t* data, uint8_t len, uint8_t* pos,
                                         const uint8_t** record, uint8_t* record_len)
{
    uint8_t value_len;

    if ((data == NULL) || (pos == NULL) || (record == NULL) || (record_len == NULL) ||
        (*pos > len) || ((uint8_t) (len - *pos) < IOLINK_MASTER_DS_RECORD_HEADER_LEN)) {
        return false;
    }

    value_len = data[(uint8_t) (*pos + (IOLINK_MASTER_DS_RECORD_HEADER_LEN - 1U))];
    if (value_len > (uint8_t) (len - *pos - IOLINK_MASTER_DS_RECORD_HEADER_LEN)) {
        return false;
    }

    *record = &data[*pos];
    *record_len = (uint8_t) (IOLINK_MASTER_DS_RECORD_HEADER_LEN + value_len);
    *pos = (uint8_t) (*pos + *record_len);
    return true;
}

/** @brief Return true if every expected DS record is present (order-independent) in the actual
 * image. */
static bool iolink_master_ds_image_contains_records(const uint8_t* actual, uint8_t actual_len,
                                                    const uint8_t* expected, uint8_t expected_len)
{
    uint8_t expected_pos = 0U;
    const uint8_t* expected_record;
    uint8_t expected_record_len;

    if ((expected == NULL) && (expected_len > 0U)) {
        return false;
    }

    while (expected_pos < expected_len) {
        uint8_t actual_pos = 0U;
        bool found = false;

        if (!iolink_master_ds_next_record(expected, expected_len, &expected_pos, &expected_record,
                                          &expected_record_len)) {
            return false;
        }

        while (actual_pos < actual_len) {
            const uint8_t* actual_record;
            uint8_t actual_record_len;

            if (!iolink_master_ds_next_record(actual, actual_len, &actual_pos, &actual_record,
                                              &actual_record_len)) {
                return false;
            }

            if ((actual_record_len == expected_record_len) &&
                (memcmp(actual_record, expected_record, expected_record_len) == 0)) {
                found = true;
                break;
            }
        }

        if (!found) {
            return false;
        }
    }

    return true;
}

/** @brief Return true if the buffer parses cleanly as a sequence of well-formed DS records. */
static bool iolink_master_ds_image_is_valid(const uint8_t* data, uint8_t len)
{
    uint8_t pos = 0U;

    if ((data == NULL) && (len > 0U)) {
        return false;
    }

    while (pos < len) {
        const uint8_t* record;
        uint8_t record_len;

        if (!iolink_master_ds_next_record(data, len, &pos, &record, &record_len)) {
            return false;
        }
        (void) record;
        (void) record_len;
    }

    return true;
}

int iolink_master_verify_data_storage(iolink_master_port_t* port, const uint8_t* expected,
                                      uint8_t len)
{
    uint8_t data[IOLINK_ISDU_BUFFER_SIZE];
    uint8_t read_len = UINT8_MAX;
    int ret;

    if ((expected == NULL) && (len > 0U)) {
        return IOLINK_MASTER_ERR_INVALID_ARG;
    }

    ret = iolink_master_read_isdu(port, IOLINK_IDX_DATA_STORAGE, 0U, data, &read_len);
    if (ret != IOLINK_MASTER_STATUS_OK) {
        return ret;
    }

    if (iolink_master_ds_image_is_valid(expected, len) &&
        iolink_master_ds_image_is_valid(data, read_len)) {
        if (!iolink_master_ds_image_contains_records(data, read_len, expected, len)) {
            return iolink_master_service_result(port, IOLINK_MASTER_ISDU_ERR_VERIFY_FAILED);
        }
    }
    else if ((read_len != len) || ((len > 0U) && (memcmp(data, expected, len) != 0))) {
        return iolink_master_service_result(port, IOLINK_MASTER_ISDU_ERR_VERIFY_FAILED);
    }
    else {
        /* Raw image matched: fall through to the success result. */
    }

    return iolink_master_service_result(port, IOLINK_MASTER_STATUS_OK);
}

int iolink_master_read_detailed_device_status(iolink_master_port_t* port, uint8_t* data,
                                              uint8_t* len)
{
    return iolink_master_read_isdu(port, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0U, data, len);
}

/** @brief Latch the final event-service result and stop the transport. */
static void iolink_master_event_finish(iolink_master_port_t* port, int result)
{
    iolink_master_port_state(port)->event.result = result;
    iolink_master_port_state(port)->event.phase = IOLINK_MASTER_EVENT_PHASE_NONE;
    iolink_master_port_state(port)->event.od_expected = false;
}

/** @brief Start an event-memory service on the diagnosis channel (7.3.8.3 T2). */
static void iolink_master_event_start(iolink_master_port_t* port,
                                      iolink_master_event_req_t request)
{
    iolink_master_event_state_t* ev = &iolink_master_port_state(port)->event;

    (void) memset(ev, 0, sizeof(*ev));
    ev->request = request;
    ev->phase = IOLINK_MASTER_EVENT_PHASE_READ;
    ev->addr = 0U;
    ev->needed = 1U; /* At least the StatusCode octet (Table 58 address 0). */
    ev->result = IOLINK_MASTER_STATUS_PENDING;
}

bool iolink_master_event_channel_access(const iolink_master_port_t* port, bool* read, uint8_t* addr,
                                        uint8_t* od_len)
{
    const iolink_master_event_state_t* ev;

    if (port == NULL) {
        return false;
    }

    ev = &iolink_master_port_const_state(port)->event;
    if (ev->phase == IOLINK_MASTER_EVENT_PHASE_NONE) {
        return false;
    }

    if (read != NULL) {
        *read = (ev->phase == IOLINK_MASTER_EVENT_PHASE_READ);
    }
    if (addr != NULL) {
        *addr = ev->addr;
    }
    if (od_len != NULL) {
        /* One OD octet per read for TYPE_0/TYPE_2 (Table A.10); TYPE_1_1/1_2/1_V
           carry their configured OD width. The address advances by the number
           of octets the device returns (Table 58 slot layout). */
        *od_len = iolink_master_port_const_state(port)->od_len;
    }
    return true;
}

void iolink_master_event_on_od(iolink_master_port_t* port, const uint8_t* od, uint8_t od_len)
{
    iolink_master_event_state_t* ev;
    uint8_t i;

    if ((port == NULL) || (od == NULL)) {
        return;
    }

    ev = &iolink_master_port_state(port)->event;
    if (ev->phase != IOLINK_MASTER_EVENT_PHASE_READ) {
        return;
    }

    for (i = 0U; i < od_len; i++) {
        if ((ev->addr < IOLINK_MASTER_EVENT_MEMORY_LEN) && (ev->len < IOLINK_MASTER_EVENT_MEMORY_LEN)) {
            ev->memory[ev->addr] = od[i];
            ev->addr++;
            ev->len++;
        }

        if (!ev->status_seen && (ev->len >= 1U)) {
            uint8_t status = ev->memory[0];
            uint8_t active = (uint8_t) (status & 0x3FU);
            uint8_t slot;

            ev->status_seen = true;
            ev->last_slot = 0U;
            for (slot = 0U; slot < IOLINK_MASTER_EVENT_SLOT_MAX; slot++) {
                if ((active & (uint8_t) (1U << slot)) != 0U) {
                    ev->last_slot = (uint8_t) (slot + 1U);
                }
            }
            /* Table 58: slot n occupies addresses 3n-2..3n. */
            ev->needed = (ev->last_slot == 0U)
                             ? 1U
                             : (uint8_t) ((IOLINK_MASTER_EVENT_ENTRY_LEN * ev->last_slot) + 1U);
        }
    }

    if (ev->status_seen && (ev->len >= ev->needed) &&
        (ev->len >= 1U)) {
        if (ev->request == IOLINK_MASTER_EVENT_REQ_ACK) {
            /* Table 59 T8: confirm the readout by writing any value to the
               StatusCode at address 0. */
            ev->phase = IOLINK_MASTER_EVENT_PHASE_WRITE;
            ev->addr = 0U;
        }
        else {
            iolink_master_event_finish(port, IOLINK_MASTER_STATUS_OK);
        }
    }
}

void iolink_master_event_on_written(iolink_master_port_t* port)
{
    if (port == NULL) {
        return;
    }

    if (iolink_master_port_state(port)->event.phase == IOLINK_MASTER_EVENT_PHASE_WRITE) {
        iolink_master_event_finish(port, IOLINK_MASTER_STATUS_OK);
    }
}

/** @brief Return true when the completed event service may be consumed by a caller. */
static bool iolink_master_event_complete(const iolink_master_port_t* port)
{
    return (iolink_master_port_const_state(port)->event.phase == IOLINK_MASTER_EVENT_PHASE_NONE) &&
           (iolink_master_port_const_state(port)->event.request != IOLINK_MASTER_EVENT_REQ_NONE);
}

/** @brief Return the EventCode of the lowest active slot in a Table 58 memory image.
 *
 * Slot n (0-based) occupies qualifier @c 1+3n, code MSB @c 2+3n and code LSB
 * @c 3+3n (Table 58). Returns 0 when no slot is active.
 */
static uint16_t iolink_master_event_first_code(const uint8_t* memory)
{
    uint8_t active = (uint8_t) (memory[0] & 0x3FU);
    uint8_t slot;

    for (slot = 0U; slot < IOLINK_MASTER_EVENT_SLOT_MAX; slot++) {
        if ((active & (uint8_t) (1U << slot)) != 0U) {
            uint8_t base = (uint8_t) (1U + (IOLINK_MASTER_EVENT_ENTRY_LEN * slot));

            return (uint16_t) (((uint16_t) memory[(uint8_t) (base + 1U)] << 8U) |
                               memory[(uint8_t) (base + 2U)]);
        }
    }

    return 0U;
}

int iolink_master_read_event_code(iolink_master_port_t* port, uint16_t* event_code)
{
    iolink_master_event_state_t* ev;

    if ((port == NULL) || (event_code == NULL)) {
        return IOLINK_MASTER_ERR_INVALID_ARG;
    }

    if ((iolink_master_port_state(port)->state != IOLINK_MASTER_STATE_OPERATE) &&
        (iolink_master_port_state(port)->state != IOLINK_MASTER_STATE_PREOPERATE)) {
        return IOLINK_MASTER_ISDU_ERR_INVALID_STATE;
    }

    ev = &iolink_master_port_state(port)->event;
    if (ev->phase != IOLINK_MASTER_EVENT_PHASE_NONE) {
        return IOLINK_MASTER_STATUS_PENDING;
    }

    if (!iolink_master_event_complete(port)) {
        iolink_master_event_start(port, IOLINK_MASTER_EVENT_REQ_CODE);
        return IOLINK_MASTER_STATUS_PENDING;
    }

    /* Consume the completed service (7.3.8.2: report the first active event). */
    *event_code = iolink_master_event_first_code(ev->memory);
    iolink_master_port_state(port)->diagnostics.last_event_code = *event_code;
    if (ev->last_slot >= 1U) {
        iolink_master_port_state(port)->diagnostics.last_event_count = 1U;
    }
    ev->request = IOLINK_MASTER_EVENT_REQ_NONE;
    return IOLINK_MASTER_STATUS_OK;
}

int iolink_master_ack_event(iolink_master_port_t* port, uint16_t* event_code)
{
    iolink_master_event_state_t* ev;

    if ((port == NULL) || (event_code == NULL)) {
        return IOLINK_MASTER_ERR_INVALID_ARG;
    }

    if ((iolink_master_port_state(port)->state != IOLINK_MASTER_STATE_OPERATE) &&
        (iolink_master_port_state(port)->state != IOLINK_MASTER_STATE_PREOPERATE)) {
        return IOLINK_MASTER_ISDU_ERR_INVALID_STATE;
    }

    ev = &iolink_master_port_state(port)->event;
    if (ev->phase != IOLINK_MASTER_EVENT_PHASE_NONE) {
        return IOLINK_MASTER_STATUS_PENDING;
    }

    if (!iolink_master_event_complete(port)) {
        iolink_master_event_start(port, IOLINK_MASTER_EVENT_REQ_ACK);
        return IOLINK_MASTER_STATUS_PENDING;
    }

    *event_code = iolink_master_event_first_code(ev->memory);
    iolink_master_port_state(port)->diagnostics.last_event_code = *event_code;
    ev->request = IOLINK_MASTER_EVENT_REQ_NONE;
    return IOLINK_MASTER_STATUS_OK;
}

/** @brief Map an event qualifier's mode field to the corresponding event type enum. */
static iolink_master_event_type_t iolink_master_event_type_from_qualifier(uint8_t qualifier)
{
    switch ((uint8_t) ((qualifier >> IOLINK_MASTER_EVENT_QUALIFIER_MODE_SHIFT) &
                       IOLINK_MASTER_EVENT_QUALIFIER_MODE_MASK)) {
        case IOLINK_MASTER_EVENT_MODE_NOTIFICATION:
            return IOLINK_MASTER_EVENT_TYPE_NOTIFICATION;
        case IOLINK_MASTER_EVENT_MODE_WARNING:
            return IOLINK_MASTER_EVENT_TYPE_WARNING;
        case IOLINK_MASTER_EVENT_MODE_ERROR:
            return IOLINK_MASTER_EVENT_TYPE_ERROR;
        default:
            return IOLINK_MASTER_EVENT_TYPE_UNKNOWN;
    }
}

int iolink_master_read_event_details(iolink_master_port_t* port, iolink_master_event_t* events,
                                     uint8_t max_events, uint8_t* out_count)
{
    iolink_master_event_state_t* ev;
    uint8_t active;
    uint8_t count = 0U;
    uint8_t slot;

    if ((port == NULL) || (events == NULL) || (out_count == NULL)) {
        return IOLINK_MASTER_ERR_INVALID_ARG;
    }

    if ((iolink_master_port_state(port)->state != IOLINK_MASTER_STATE_OPERATE) &&
        (iolink_master_port_state(port)->state != IOLINK_MASTER_STATE_PREOPERATE)) {
        return IOLINK_MASTER_ISDU_ERR_INVALID_STATE;
    }

    ev = &iolink_master_port_state(port)->event;
    if (ev->phase != IOLINK_MASTER_EVENT_PHASE_NONE) {
        return IOLINK_MASTER_STATUS_PENDING;
    }

    if (!iolink_master_event_complete(port)) {
        iolink_master_event_start(port, IOLINK_MASTER_EVENT_REQ_DETAILS);
        return IOLINK_MASTER_STATUS_PENDING;
    }

    /* Table 58: Active slots are reported in the StatusCode bits 0-5. */
    active = (uint8_t) (ev->memory[0] & 0x3FU);
    count = 0U;
    for (slot = 0U; slot < IOLINK_MASTER_EVENT_SLOT_MAX; slot++) {
        uint8_t base;

        if ((active & (uint8_t) (1U << slot)) == 0U) {
            continue;
        }
        base = (uint8_t) (1U + (IOLINK_MASTER_EVENT_ENTRY_LEN * slot));
        events[count].qualifier = ev->memory[base];
        events[count].type = iolink_master_event_type_from_qualifier(events[count].qualifier);
        events[count].code = (uint16_t) (((uint16_t) ev->memory[(uint8_t) (base + 1U)] << 8U) |
                                         ev->memory[(uint8_t) (base + 2U)]);
        count++;
        if (count >= max_events) {
            if (count < IOLINK_MASTER_MAX_EVENTS) {
                /* Report the events decoded so far but signal the overflow. */
                break;
            }
            break;
        }
    }

    *out_count = count;
    iolink_master_port_state(port)->diagnostics.last_event_count = count;
    iolink_master_port_state(port)->diagnostics.last_event_code =
        (count > 0U) ? events[count - 1U].code : 0U;

    if (iolink_master_port_state(port)->config.event_handler != NULL) {
        uint8_t i;

        for (i = 0U; i < count; i++) {
            iolink_master_port_state(port)->config.event_handler(
                iolink_master_port_state(port)->config.event_user, &events[i]);
        }
    }

    ev->request = IOLINK_MASTER_EVENT_REQ_NONE;
    return IOLINK_MASTER_STATUS_OK;
}

/** @brief Write a single-byte SystemCommand value to the device via ISDU. */
static int iolink_master_write_system_command(iolink_master_port_t* port, uint8_t command)
{
    return iolink_master_write_isdu(port, IOLINK_IDX_SYSTEM_COMMAND, 0U, &command, 1U);
}

int iolink_master_begin_parameter_download(iolink_master_port_t* port)
{
    return iolink_master_write_system_command(port, IOLINK_CMD_PARAM_DOWNLOAD_START);
}

int iolink_master_end_parameter_download(iolink_master_port_t* port)
{
    return iolink_master_write_system_command(port, IOLINK_CMD_PARAM_DOWNLOAD_END);
}

int iolink_master_begin_parameter_upload(iolink_master_port_t* port)
{
    return iolink_master_write_system_command(port, IOLINK_CMD_PARAM_UPLOAD_START);
}

int iolink_master_end_parameter_upload(iolink_master_port_t* port)
{
    return iolink_master_write_system_command(port, IOLINK_CMD_PARAM_UPLOAD_END);
}

int iolink_master_store_parameter_download(iolink_master_port_t* port)
{
    return iolink_master_write_system_command(port, IOLINK_CMD_PARAM_DOWNLOAD_STORE);
}

/** @brief Return true if the latched parameter-block transfer matches the given
 * index/subindex/data. */
static bool iolink_master_block_matches(const iolink_master_port_t* port, uint16_t index,
                                        uint8_t subindex, const uint8_t* data, uint8_t len)
{
    const iolink_master_block_state_t* block = &iolink_master_port_const_state(port)->block;

    return (block->index == index) && (block->subindex == subindex) && (block->len == len) &&
           ((len == 0U) || (memcmp(block->data, data, len) == 0));
}

/** @brief Reset the parameter-block transfer state to idle. */
static void iolink_master_block_clear(iolink_master_port_t* port)
{
    (void) memset(&iolink_master_port_state(port)->block, 0,
                  sizeof(iolink_master_port_state(port)->block));
}

int iolink_master_write_parameter_block(iolink_master_port_t* port, uint16_t index,
                                        uint8_t subindex, const uint8_t* data, uint8_t len)
{
    iolink_master_block_state_t* block;
    int ret;

    if ((port == NULL) || ((data == NULL) && (len > 0U))) {
        return IOLINK_MASTER_ERR_INVALID_ARG;
    }

    if (len > (uint8_t) (IOLINK_ISDU_BUFFER_SIZE - IOLINK_MASTER_ISDU_WRITE_HEADER_MAX)) {
        return IOLINK_MASTER_ISDU_ERR_BUFFER_TOO_SMALL;
    }

    block = &iolink_master_port_state(port)->block;
    if (block->step == IOLINK_MASTER_BLOCK_STEP_NONE) {
        block->step = IOLINK_MASTER_BLOCK_STEP_BEGIN_DOWNLOAD;
        block->index = index;
        block->subindex = subindex;
        block->len = len;
        if (len > 0U) {
            (void) memcpy(block->data, data, len);
        }
    }
    else if (!iolink_master_block_matches(port, index, subindex, data, len)) {
        return iolink_master_service_result(port, IOLINK_MASTER_ISDU_ERR_BUSY);
    }
    else {
        /* Resuming the same block transfer: keep the latched state. */
    }

    if (block->step == IOLINK_MASTER_BLOCK_STEP_BEGIN_DOWNLOAD) {
        ret = iolink_master_begin_parameter_download(port);
        if (ret != IOLINK_MASTER_STATUS_OK) {
            return ret;
        }
        block->step = IOLINK_MASTER_BLOCK_STEP_WRITE;
    }

    if (block->step == IOLINK_MASTER_BLOCK_STEP_WRITE) {
        ret =
            iolink_master_write_isdu(port, block->index, block->subindex, block->data, block->len);
        if (ret != IOLINK_MASTER_STATUS_OK) {
            return ret;
        }
        block->step = IOLINK_MASTER_BLOCK_STEP_END_DOWNLOAD;
    }

    if (block->step == IOLINK_MASTER_BLOCK_STEP_END_DOWNLOAD) {
        ret = iolink_master_end_parameter_download(port);
        if (ret != IOLINK_MASTER_STATUS_OK) {
            return ret;
        }
        block->step = IOLINK_MASTER_BLOCK_STEP_VERIFY;
    }

    if ((block->index == IOLINK_IDX_DATA_STORAGE) && (block->subindex == 0U)) {
        ret = iolink_master_verify_data_storage(port, block->data, block->len);
    }
    else {
        ret =
            iolink_master_verify_isdu(port, block->index, block->subindex, block->data, block->len);
    }
    if (ret == IOLINK_MASTER_STATUS_OK) {
        iolink_master_block_clear(port);
    }
    else if (ret < 0) {
        iolink_master_block_clear(port);
    }
    else {
        /* Still pending: keep the block state for the next call. */
    }

    return iolink_master_service_result(port, ret);
}
