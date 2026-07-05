/**
 * @file master_internal.h
 * @brief Private internal state and named constants for the IO-Link master stack.
 *
 * This header is not part of the public ABI. It defines the master-owned named
 * constants (wire offsets, framing budgets, encoding masks), the private
 * per-port and controller state structures backed by the public opaque
 * storage, the static-assert size guards, and the inline accessors that
 * reinterpret the caller-owned opaque storage as the private state.
 */
#ifndef IOLINKI_MASTER_INTERNAL_H
#define IOLINKI_MASTER_INTERNAL_H

#include "iolinki_master/master.h"

/**
 * @defgroup iolinki_master_internal Master Internal State
 * @brief Private internal state, constants and accessors for the master stack.
 * @{
 */

/*
 * Named constants for the master stack. These are master-owned (they intentionally
 * do not modify the shared device-stack protocol.h); values that already have a
 * name in iolinki/protocol.h are reused rather than redefined here.
 */

/** @brief RX/TX scratch buffer size; must hold the worst-case operate frame. */
#define IOLINK_MASTER_FRAME_BUF_SIZE 64U
/** @brief Checksum/response retry budget before entering the error state. */
#define IOLINK_MASTER_RX_RETRY_LIMIT 2U
/** @brief Wake-up request pattern (alternating bits) emitted when no wake_up hook is set. */
#define IOLINK_MASTER_WAKEUP_BYTE 0x55U

/** @name Direct Parameter Page 1 wire layout (see IO-Link spec Table B.1).
 *  @{
 */
#define IOLINK_MASTER_DPP1_LEN 16U                  /**< Direct Parameter Page 1 length, in bytes. */
#define IOLINK_MASTER_DPP1_OFF_MASTER_COMMAND 0x00U /**< Offset of the MasterCommand octet. */
#define IOLINK_MASTER_DPP1_OFF_MIN_CYCLE_TIME 0x02U /**< Offset of the MinCycleTime octet. */
#define IOLINK_MASTER_DPP1_OFF_MSEQ_CAPABILITY 0x03U/**< Offset of the M-sequenceCapability octet. */
#define IOLINK_MASTER_DPP1_OFF_REVISION_ID 0x04U    /**< Offset of the RevisionID octet. */
#define IOLINK_MASTER_DPP1_OFF_PD_IN_DESC 0x05U     /**< Offset of the input PD descriptor octet. */
#define IOLINK_MASTER_DPP1_OFF_PD_OUT_DESC 0x06U    /**< Offset of the output PD descriptor octet. */
#define IOLINK_MASTER_DPP1_OFF_VENDOR_ID_HI 0x07U   /**< Offset of the VendorID high octet. */
#define IOLINK_MASTER_DPP1_OFF_VENDOR_ID_LO 0x08U   /**< Offset of the VendorID low octet. */
#define IOLINK_MASTER_DPP1_OFF_DEVICE_ID_HI 0x09U   /**< Offset of the DeviceID high octet. */
#define IOLINK_MASTER_DPP1_OFF_DEVICE_ID_MID 0x0AU  /**< Offset of the DeviceID middle octet. */
#define IOLINK_MASTER_DPP1_OFF_DEVICE_ID_LO 0x0BU   /**< Offset of the DeviceID low octet. */
/** @} */

/** @name IO-Link protocol revision IDs (RevisionID octet, Figure B.4).
 *  @{
 */
#define IOLINK_MASTER_REVISION_1_0 0x10U /**< RevisionID for IO-Link 1.0. */
#define IOLINK_MASTER_REVISION_1_1 0x11U /**< RevisionID for IO-Link 1.1. */
/** @} */

/** @name MinCycleTime octet fields (Figure B.2 / Table B.3).
 *  @{
 */
#define IOLINK_MASTER_MIN_CYCLE_BASE_SHIFT 6U  /**< Shift of the time-base field. */
#define IOLINK_MASTER_MIN_CYCLE_BASE_MASK 0x03U /**< Mask of the time-base field (post-shift). */
#define IOLINK_MASTER_MIN_CYCLE_MULT_MASK 0x3FU /**< Mask of the multiplier field. */
/** @} */

/** @name M-sequenceCapability octet bit fields (Figure B.3).
 *  @{
 */
#define IOLINK_MASTER_MSEQ_CAP_ISDU_BIT 0x01U    /**< ISDU-supported bit. */
#define IOLINK_MASTER_MSEQ_CAP_OPERATE_SHIFT 1U  /**< Shift of the OPERATE M-sequence code field. */
#define IOLINK_MASTER_MSEQ_CAP_OPERATE_MASK 0x07U/**< Mask of the OPERATE M-sequence code field. */
#define IOLINK_MASTER_MSEQ_CAP_PREOP_SHIFT 4U    /**< Shift of the PREOPERATE M-sequence code field. */
#define IOLINK_MASTER_MSEQ_CAP_PREOP_MASK 0x03U  /**< Mask of the PREOPERATE M-sequence code field. */
/** @} */

/** @name ProcessData descriptor octet fields (Figure B.5 / Table B.6).
 *  @{
 */
#define IOLINK_MASTER_PD_DESC_BYTE_BIT 0x80U      /**< Byte/bit length-unit selector bit. */
#define IOLINK_MASTER_PD_DESC_LENGTH_MASK 0x1FU   /**< Mask of the PD length field. */
#define IOLINK_MASTER_PD_DESC_BITS_PER_OCTET 8U   /**< Bits per octet for bit-length conversion. */
/** @} */

/** @brief Master Command comm-channel field position (pairs with IOLINK_MC_COMM_CHANNEL_MASK). */
#define IOLINK_MASTER_MC_COMM_CHANNEL_SHIFT 5U

/** @name ISDU framing.
 *  @{
 */
#define IOLINK_MASTER_ISDU_SERVICE_SHIFT 4U      /**< Shift of the ISDU service nibble. */
#define IOLINK_MASTER_ISDU_RESPONSE_ERROR 0x80U  /**< Bit marking an ISDU error response. */
#define IOLINK_MASTER_ISDU_LENGTH_NIBBLE_MAX 15U /**< Max length representable in the length nibble. */
#define IOLINK_MASTER_ISDU_LENGTH_EXTENDED 0x0FU /**< Length nibble value selecting extended length. */
#define IOLINK_MASTER_ISDU_WRITE_HEADER_MAX 5U   /**< Max ISDU write header length, in bytes. */
#define IOLINK_MASTER_ISDU_READ_HEADER_LEN 4U    /**< ISDU read header length, in bytes. */
/** @} */

/** @name Data Storage record header + event-entry framing.
 *  @{
 */
#define IOLINK_MASTER_DS_RECORD_HEADER_LEN 4U         /**< Data Storage record header length, in bytes. */
#define IOLINK_MASTER_EVENT_ENTRY_LEN 3U              /**< Length of one event entry, in bytes. */
#define IOLINK_MASTER_MAX_EVENTS 8U                   /**< Maximum events decoded per read. */
#define IOLINK_MASTER_EVENT_QUALIFIER_MODE_SHIFT 4U   /**< Shift of the event-qualifier mode field. */
#define IOLINK_MASTER_EVENT_QUALIFIER_MODE_MASK 0x03U /**< Mask of the event-qualifier mode field. */
#define IOLINK_MASTER_EVENT_MODE_NOTIFICATION 1U      /**< Qualifier mode: notification. */
#define IOLINK_MASTER_EVENT_MODE_WARNING 2U           /**< Qualifier mode: warning. */
#define IOLINK_MASTER_EVENT_MODE_ERROR 3U             /**< Qualifier mode: error. */
/** @} */

/** @name Startup micro-sequence steps (state of iolink_master_startup_state_t.step).
 *  @{
 */
#define IOLINK_MASTER_STARTUP_STEP_WAKE 0U           /**< Emit the wake-up request. */
#define IOLINK_MASTER_STARTUP_STEP_SEND_TYPE0 1U     /**< Send the TYPE_0 request. */
#define IOLINK_MASTER_STARTUP_STEP_AWAIT_RESPONSE 2U /**< Await the device response. */
/** @} */

/** @brief Startup micro-sequence progress for a port. */
typedef struct
{
    uint8_t step;           /**< Current startup step (IOLINK_MASTER_STARTUP_STEP_*). */
    uint8_t baudrate_index; /**< Index into the COM baudrate sweep (auto-baud). */
    uint8_t wake_attempts;  /**< Wake-up requests issued at the current baudrate. */
} iolink_master_startup_state_t;

/** @brief In-flight ISDU request/response state machine for a port. */
typedef struct
{
    iolink_master_isdu_op_t op;                  /**< Current ISDU operation kind. */
    uint16_t index;                              /**< ISDU index being accessed. */
    uint8_t subindex;                            /**< ISDU subindex being accessed. */
    uint8_t request[IOLINK_ISDU_BUFFER_SIZE];    /**< Assembled ISDU request buffer. */
    uint8_t request_len;                         /**< Total request length, in bytes. */
    uint8_t request_pos;                         /**< Bytes of the request already sent. */
    uint8_t request_seq;                         /**< Request flow-control sequence counter. */
    bool request_control_phase;                  /**< True while in the request control phase. */
    bool request_sent;                           /**< True once the full request is sent. */
    uint8_t response[IOLINK_ISDU_BUFFER_SIZE];   /**< Assembled ISDU response buffer. */
    uint16_t response_len;                        /**< Total response length, in bytes. */
    uint8_t response_seq;                         /**< Response flow-control sequence counter. */
    bool response_expect_control;                 /**< True while expecting a response control octet. */
    bool response_last;                           /**< True on the final response segment. */
    bool done;                                    /**< True once the operation has completed. */
    uint8_t error;                                /**< ISDU error code (0 = none). */
} iolink_master_isdu_state_t;

/** @brief Receive assembly buffer for a port. */
typedef struct
{
    uint8_t buf[IOLINK_MASTER_FRAME_BUF_SIZE]; /**< Accumulated receive bytes. */
    uint8_t len;                               /**< Number of valid bytes in @c buf. */
} iolink_master_rx_state_t;

/** @brief Step of a block up/download parameter-server operation. */
typedef enum
{
    IOLINK_MASTER_BLOCK_STEP_NONE = 0,           /**< No block operation active. */
    IOLINK_MASTER_BLOCK_STEP_BEGIN_DOWNLOAD = 1, /**< Begin-download command pending/active. */
    IOLINK_MASTER_BLOCK_STEP_WRITE = 2,          /**< Payload write pending/active. */
    IOLINK_MASTER_BLOCK_STEP_END_DOWNLOAD = 3,   /**< End-download command pending/active. */
    IOLINK_MASTER_BLOCK_STEP_VERIFY = 4          /**< Readback verification pending/active. */
} iolink_master_block_step_t;

/** @brief State for a multi-step parameter block write. */
typedef struct
{
    iolink_master_block_step_t step;        /**< Current block-operation step. */
    uint16_t index;                         /**< Target ISDU index. */
    uint8_t subindex;                       /**< Target ISDU subindex. */
    uint8_t data[IOLINK_ISDU_BUFFER_SIZE];  /**< Payload to write / verify. */
    uint8_t len;                            /**< Payload length, in bytes. */
} iolink_master_block_state_t;

/** @brief Private per-port state backed by ::iolink_master_port_t storage. */
typedef struct
{
    const iolink_phy_api_t* phy;                  /**< PHY API, retained by pointer (not copied). */
    iolink_master_config_t config;                /**< Copy of the port configuration. */
    iolink_master_state_t state;                  /**< Current communication state. */
    uint8_t od_len;                               /**< On-request-data length for the M-sequence. */
    uint8_t tx_buf[IOLINK_MASTER_FRAME_BUF_SIZE]; /**< Transmit scratch buffer. */
    uint8_t pd_in[IOLINK_PD_IN_MAX_SIZE];         /**< Latest input process data. */
    uint8_t pd_in_len;                            /**< Input process-data length, in bytes. */
    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE];       /**< Staged output process data. */
    uint8_t pd_out_len;                           /**< Output process-data length, in bytes. */
    bool pd_valid;                                /**< True when @c pd_in holds valid data. */
    iolink_master_startup_state_t startup;        /**< Startup micro-sequence state. */
    iolink_master_diagnostics_t diagnostics;      /**< Runtime diagnostics counters. */
    iolink_master_device_info_t device_info;      /**< Decoded device identification. */
    iolink_master_isdu_state_t isdu;              /**< In-flight ISDU state machine. */
    iolink_master_block_state_t block;            /**< Parameter block operation state. */
    iolink_master_rx_state_t rx;                  /**< Receive assembly buffer. */
    uint32_t cycle_count;                         /**< Number of cycles executed. */
    uint32_t last_cycle_start_100us;              /**< Last cycle start timestamp, in 100us units. */
    uint32_t response_deadline_100us;             /**< Response deadline timestamp, in 100us units. */
    bool cycle_timer_valid;                       /**< True once the cycle timer has a valid start. */
    bool awaiting_response;                       /**< True while waiting on a device response. */
} iolink_master_port_state_t;

/** @brief Private controller state backed by ::iolink_master_controller_t storage. */
typedef struct
{
    iolink_master_port_t* ports; /**< Caller-owned port array reference. */
    uint8_t port_count;          /**< Number of managed ports. */
} iolink_master_controller_state_t;

/** @brief Compile-time assert that the port state fits its opaque storage budget. */
typedef char iolink_master_port_storage_must_fit
    [(sizeof(iolink_master_port_state_t) <= IOLINK_MASTER_PORT_STORAGE_SIZE) ? 1 : -1];
/** @brief Compile-time assert that the controller state fits its opaque storage budget. */
typedef char iolink_master_controller_storage_must_fit
    [(sizeof(iolink_master_controller_state_t) <= IOLINK_MASTER_CONTROLLER_STORAGE_SIZE) ? 1 : -1];

/*
 * These accessors reinterpret the caller-owned opaque storage as the private
 * state struct. The `void*` cast is a deliberate, documented deviation from
 * MISRA C:2012 Rule 11.5: the public ABI keeps the state opaque and heap-free,
 * and the `_storage_must_fit` static asserts above guarantee the storage is
 * large enough (and the union alignment members in master.h guarantee
 * alignment). See docs/MISRA_DEVIATIONS.md.
 */

/** @brief Reinterpret opaque port storage as mutable private port state. */
static inline iolink_master_port_state_t* iolink_master_port_state(iolink_master_port_t* port)
{
    return (iolink_master_port_state_t*)(void*)port->storage;
}

/** @brief Reinterpret opaque port storage as const private port state. */
static inline const iolink_master_port_state_t*
iolink_master_port_const_state(const iolink_master_port_t* port)
{
    return (const iolink_master_port_state_t*)(const void*)port->storage;
}

/** @brief Reinterpret opaque controller storage as mutable private controller state. */
static inline iolink_master_controller_state_t*
iolink_master_controller_state(iolink_master_controller_t* controller)
{
    return (iolink_master_controller_state_t*)(void*)controller->storage;
}

/** @brief Reinterpret opaque controller storage as const private controller state. */
static inline const iolink_master_controller_state_t*
iolink_master_controller_const_state(const iolink_master_controller_t* controller)
{
    return (const iolink_master_controller_state_t*)(const void*)controller->storage;
}

/** @brief Return the on-request-data length in octets for an M-sequence type. */
static inline uint8_t iolink_master_od_len_for_type(iolink_master_m_seq_type_t type)
{
    switch(type)
    {
    case IOLINK_MASTER_M_SEQ_TYPE_2_1:
    case IOLINK_MASTER_M_SEQ_TYPE_2_2:
    case IOLINK_MASTER_M_SEQ_TYPE_2_V:
        return 2U;
    default:
        return 1U;
    }
}

/** @brief Test whether a port's response deadline has elapsed at @p now_100us. */
static inline bool iolink_master_response_due_at(const iolink_master_port_t* port,
                                                 uint32_t now_100us)
{
    const iolink_master_port_state_t* state = iolink_master_port_const_state(port);

    return state->awaiting_response && (now_100us >= state->response_deadline_100us);
}

/** @brief Fill the on-request-data octets of an outgoing frame from ISDU state. */
void iolink_master_isdu_fill_od(iolink_master_port_t* port, uint8_t* od, uint8_t od_len);
/** @brief Consume the on-request-data octets of a received frame into ISDU state. */
void iolink_master_isdu_on_od(iolink_master_port_t* port, const uint8_t* od, uint8_t od_len);

/** @} */ /* end of iolinki_master_internal group */

#endif /* IOLINKI_MASTER_INTERNAL_H */
