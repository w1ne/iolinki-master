#ifndef IOLINKI_MASTER_MASTER_H
#define IOLINKI_MASTER_MASTER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "iolinki/config.h"
#include "iolinki/phy.h"

typedef enum
{
    IOLINK_MASTER_M_SEQ_TYPE_0 = 0U,
    IOLINK_MASTER_M_SEQ_TYPE_1_1 = 1U,
    IOLINK_MASTER_M_SEQ_TYPE_1_2 = 2U,
    IOLINK_MASTER_M_SEQ_TYPE_1_V = 3U,
    IOLINK_MASTER_M_SEQ_TYPE_2_1 = 4U,
    IOLINK_MASTER_M_SEQ_TYPE_2_2 = 5U,
    IOLINK_MASTER_M_SEQ_TYPE_2_V = 6U,
} iolink_master_m_seq_type_t;

typedef enum
{
    IOLINK_MASTER_STATE_INACTIVE = 0,
    IOLINK_MASTER_STATE_STARTUP = 1,
    IOLINK_MASTER_STATE_PREOPERATE = 2,
    IOLINK_MASTER_STATE_OPERATE = 3,
    IOLINK_MASTER_STATE_ERROR = 4
} iolink_master_state_t;

typedef enum
{
    IOLINK_MASTER_PORT_MODE_IOLINK = 0,
    IOLINK_MASTER_PORT_MODE_DI = 1,
    IOLINK_MASTER_PORT_MODE_DQ = 2,
    IOLINK_MASTER_PORT_MODE_DEACTIVATED = 3
} iolink_master_port_mode_t;

typedef enum
{
    IOLINK_MASTER_ISDU_OP_NONE = 0,
    IOLINK_MASTER_ISDU_OP_READ = 1,
    IOLINK_MASTER_ISDU_OP_WRITE = 2
} iolink_master_isdu_op_t;

typedef struct
{
    iolink_master_port_mode_t port_mode;
    iolink_master_m_seq_type_t m_seq_type;
    iolink_baudrate_t baudrate;
    uint8_t min_cycle_time;
    uint8_t pd_in_len;
    uint8_t pd_out_len;
    bool auto_baudrate;
} iolink_master_config_t;

typedef struct
{
    uint8_t step;
    uint8_t baudrate_index;
} iolink_master_startup_t;

typedef struct
{
    uint8_t od_status;
    bool event_pending;
    uint8_t rx_retry_count;
    uint32_t checksum_errors;
    uint32_t send_errors;
} iolink_master_diagnostics_t;

typedef struct
{
    iolink_master_isdu_op_t op;
    uint16_t index;
    uint8_t subindex;
    uint8_t request[IOLINK_ISDU_BUFFER_SIZE];
    uint8_t request_len;
    uint8_t request_pos;
    uint8_t request_seq;
    bool request_control_phase;
    bool request_sent;
    uint8_t response[IOLINK_ISDU_BUFFER_SIZE];
    uint16_t response_len;
    uint8_t response_seq;
    bool response_expect_control;
    bool response_last;
    bool done;
    uint8_t error;
} iolink_master_isdu_t;

typedef struct
{
    uint8_t buf[64];
    uint8_t len;
} iolink_master_rx_t;

typedef struct
{
    const iolink_phy_api_t* phy;
    iolink_master_config_t config;
    iolink_master_state_t state;
    uint8_t od_len;
    uint8_t tx_buf[64];
    uint8_t pd_in[IOLINK_PD_IN_MAX_SIZE];
    uint8_t pd_in_len;
    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE];
    uint8_t pd_out_len;
    bool pd_valid;
    iolink_master_startup_t startup;
    iolink_master_diagnostics_t diagnostics;
    iolink_master_isdu_t isdu;
    iolink_master_rx_t rx;
    uint32_t cycle_count;
} iolink_master_port_t;

typedef struct
{
    iolink_master_port_t* ports;
    uint8_t port_count;
} iolink_master_controller_t;

int iolink_master_init(iolink_master_port_t* port,
                       const iolink_phy_api_t* phy,
                       const iolink_master_config_t* config);
int iolink_master_restart(iolink_master_port_t* port);
void iolink_master_process(iolink_master_port_t* port);
int iolink_master_poll_rx(iolink_master_port_t* port);
int iolink_master_on_timeout(iolink_master_port_t* port);
int iolink_master_tick(iolink_master_port_t* port, bool response_timeout);
int iolink_master_on_rx(iolink_master_port_t* port, const uint8_t* data, uint8_t len);
iolink_master_state_t iolink_master_get_state(const iolink_master_port_t* port);
int iolink_master_get_pd_in(const iolink_master_port_t* port,
                            uint8_t* buffer,
                            uint8_t buffer_len,
                            uint8_t* out_len);
int iolink_master_get_od_status(const iolink_master_port_t* port, uint8_t* status);
uint8_t iolink_master_get_device_status(const iolink_master_port_t* port);
int iolink_master_set_pd_out(iolink_master_port_t* port, const uint8_t* data, uint8_t len);
int iolink_master_read_isdu(iolink_master_port_t* port,
                            uint16_t index,
                            uint8_t subindex,
                            uint8_t* data,
                            uint8_t* len);
int iolink_master_write_isdu(iolink_master_port_t* port,
                             uint16_t index,
                             uint8_t subindex,
                             const uint8_t* data,
                             uint8_t len);
int iolink_master_controller_init(iolink_master_controller_t* controller,
                                  iolink_master_port_t* ports,
                                  uint8_t port_count,
                                  const iolink_phy_api_t* phys,
                                  const iolink_master_config_t* configs);
int iolink_master_controller_tick(iolink_master_controller_t* controller,
                                  const bool* response_timeouts);

#endif /* IOLINKI_MASTER_MASTER_H */
