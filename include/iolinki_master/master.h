#ifndef IOLINKI_MASTER_MASTER_H
#define IOLINKI_MASTER_MASTER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "iolinki/iolink.h"
#include "iolinki/phy.h"

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
    iolink_m_seq_type_t m_seq_type;
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
    bool pd_valid;
    uint8_t startup_step;
    uint32_t cycle_count;
    uint32_t checksum_errors;
} iolink_master_port_t;

int iolink_master_init(iolink_master_port_t* port,
                       const iolink_phy_api_t* phy,
                       const iolink_master_config_t* config);
void iolink_master_process(iolink_master_port_t* port);
iolink_master_state_t iolink_master_get_state(const iolink_master_port_t* port);
int iolink_master_get_pd_in(const iolink_master_port_t* port,
                            uint8_t* buffer,
                            uint8_t buffer_len,
                            uint8_t* out_len);

#endif /* IOLINKI_MASTER_MASTER_H */
