#ifndef IOLINKI_MASTER_INTERNAL_H
#define IOLINKI_MASTER_INTERNAL_H

#include "iolinki_master/master.h"

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

void iolink_master_isdu_fill_od(iolink_master_port_t* port, uint8_t* od, uint8_t od_len);
void iolink_master_isdu_on_od(iolink_master_port_t* port, const uint8_t* od, uint8_t od_len);

#endif /* IOLINKI_MASTER_INTERNAL_H */
