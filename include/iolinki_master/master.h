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

typedef enum
{
    IOLINK_MASTER_TICK_NONE = 0,
    IOLINK_MASTER_TICK_CYCLE_DUE = 1,
    IOLINK_MASTER_TICK_RESPONSE_TIMEOUT = 2
} iolink_master_tick_event_t;

typedef struct
{
    iolink_master_port_mode_t port_mode;
    iolink_master_m_seq_type_t m_seq_type;
    iolink_baudrate_t baudrate;
    uint8_t min_cycle_time;
    uint8_t pd_in_len;
    uint8_t pd_out_len;
    bool auto_baudrate;
    bool validate_device_info;
} iolink_master_config_t;

typedef struct
{
    uint8_t od_status;
    bool event_pending;
    uint8_t rx_retry_count;
    uint32_t checksum_errors;
    uint32_t send_errors;
    uint32_t response_timeouts;
    uint32_t cycle_slips;
} iolink_master_diagnostics_t;

typedef struct
{
    bool valid;
    uint8_t min_cycle_time;
    uint8_t mseq_capability;
    bool isdu_supported;
    uint8_t operate_mseq_code;
    uint8_t preoperate_mseq_code;
    uint8_t revision_id;
    uint8_t pd_in_descriptor;
    uint8_t pd_out_descriptor;
    uint8_t pd_in_len;
    uint8_t pd_out_len;
    uint16_t vendor_id;
    uint32_t device_id;
} iolink_master_device_info_t;

#define IOLINK_MASTER_PORT_STORAGE_SIZE 1024U
#define IOLINK_MASTER_CONTROLLER_STORAGE_SIZE 32U

typedef union
{
    void* align_ptr;
    uint32_t align_u32;
    uint8_t storage[IOLINK_MASTER_PORT_STORAGE_SIZE];
} iolink_master_port_t;

typedef union
{
    void* align_ptr;
    uint32_t align_u32;
    uint8_t storage[IOLINK_MASTER_CONTROLLER_STORAGE_SIZE];
} iolink_master_controller_t;

int iolink_master_init(iolink_master_port_t* port,
                       const iolink_phy_api_t* phy,
                       const iolink_master_config_t* config);
int iolink_master_restart(iolink_master_port_t* port);
void iolink_master_process(iolink_master_port_t* port);
int iolink_master_poll_rx(iolink_master_port_t* port);
int iolink_master_on_timeout(iolink_master_port_t* port);
int iolink_master_tick(iolink_master_port_t* port, bool response_timeout);
int iolink_master_tick_event(iolink_master_port_t* port, iolink_master_tick_event_t event);
int iolink_master_tick_at(iolink_master_port_t* port,
                          iolink_master_tick_event_t event,
                          uint32_t now_100us);
int iolink_master_on_rx(iolink_master_port_t* port, const uint8_t* data, uint8_t len);
iolink_master_state_t iolink_master_get_state(const iolink_master_port_t* port);
int iolink_master_get_pd_in(const iolink_master_port_t* port,
                            uint8_t* buffer,
                            uint8_t buffer_len,
                            uint8_t* out_len);
int iolink_master_get_od_status(const iolink_master_port_t* port, uint8_t* status);
uint8_t iolink_master_get_device_status(const iolink_master_port_t* port);
int iolink_master_get_diagnostics(const iolink_master_port_t* port,
                                  iolink_master_diagnostics_t* diagnostics);
int iolink_master_set_dq(iolink_master_port_t* port, bool level);
int iolink_master_parse_direct_parameter_page1(const uint8_t* page,
                                               uint8_t len,
                                               iolink_master_device_info_t* info);
int iolink_master_apply_direct_parameter_page1(iolink_master_port_t* port,
                                               const uint8_t* page,
                                               uint8_t len);
int iolink_master_get_device_info(const iolink_master_port_t* port,
                                  iolink_master_device_info_t* info);
int iolink_master_validate_device_info(const iolink_master_port_t* port);
int iolink_master_set_pd_out(iolink_master_port_t* port, const uint8_t* data, uint8_t len);
int iolink_master_read_isdu(iolink_master_port_t* port,
                            uint16_t index,
                            uint8_t subindex,
                            uint8_t* data,
                            uint8_t* len);
int iolink_master_read_device_info(iolink_master_port_t* port);
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
int iolink_master_controller_tick_events(iolink_master_controller_t* controller,
                                         const iolink_master_tick_event_t* events);
int iolink_master_controller_tick_at(iolink_master_controller_t* controller,
                                     uint32_t now_100us);

#endif /* IOLINKI_MASTER_MASTER_H */
