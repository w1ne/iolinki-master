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

typedef struct
{
    iolink_master_m_seq_type_t m_seq_type;
    iolink_baudrate_t baudrate;
    uint8_t min_cycle_time;
    uint8_t pd_in_len;
    uint8_t pd_out_len;
} iolink_master_config_t;

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
    uint8_t startup_step;
    uint32_t cycle_count;
    uint32_t checksum_errors;
    uint32_t send_errors;
} iolink_master_port_t;

int iolink_master_init(iolink_master_port_t* port,
                       const iolink_phy_api_t* phy,
                       const iolink_master_config_t* config);
void iolink_master_process(iolink_master_port_t* port);
int iolink_master_on_rx(iolink_master_port_t* port, const uint8_t* data, uint8_t len);
iolink_master_state_t iolink_master_get_state(const iolink_master_port_t* port);
int iolink_master_get_pd_in(const iolink_master_port_t* port,
                            uint8_t* buffer,
                            uint8_t buffer_len,
                            uint8_t* out_len);
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

#endif /* IOLINKI_MASTER_MASTER_H */
