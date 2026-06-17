#include "master_internal.h"

#include <string.h>

static uint8_t iolink_master_decode_pd_descriptor(uint8_t descriptor)
{
    if(descriptor == 0U)
    {
        return 0U;
    }

    if((descriptor & 0x80U) != 0U)
    {
        return (uint8_t)((descriptor & 0x7FU) + 1U);
    }

    return (uint8_t)(descriptor / 8U);
}

static uint8_t iolink_master_mseq_capability_code(iolink_master_m_seq_type_t type)
{
    switch(type)
    {
    case IOLINK_MASTER_M_SEQ_TYPE_1_1:
    case IOLINK_MASTER_M_SEQ_TYPE_1_2:
        return 1U;
    case IOLINK_MASTER_M_SEQ_TYPE_1_V:
    case IOLINK_MASTER_M_SEQ_TYPE_2_V:
        return 5U;
    default:
        return 0U;
    }
}

int iolink_master_parse_direct_parameter_page1(const uint8_t* page,
                                               uint8_t len,
                                               iolink_master_device_info_t* info)
{
    if((page == NULL) || (info == NULL))
    {
        return -1;
    }

    if(len < 16U)
    {
        return -2;
    }

    memset(info, 0, sizeof(*info));
    info->valid = true;
    info->min_cycle_time = page[0x02];
    info->mseq_capability = page[0x03];
    info->isdu_supported = ((page[0x03] & 0x01U) != 0U);
    info->operate_mseq_code = (uint8_t)((page[0x03] >> 1U) & 0x07U);
    info->preoperate_mseq_code = (uint8_t)((page[0x03] >> 4U) & 0x03U);
    info->revision_id = page[0x04];
    info->pd_in_descriptor = page[0x05];
    info->pd_out_descriptor = page[0x06];
    info->pd_in_len = iolink_master_decode_pd_descriptor(page[0x05]);
    info->pd_out_len = iolink_master_decode_pd_descriptor(page[0x06]);
    info->vendor_id = (uint16_t)(((uint16_t)page[0x07] << 8U) | page[0x08]);
    info->device_id = ((uint32_t)page[0x09] << 16U) | ((uint32_t)page[0x0A] << 8U) |
                      (uint32_t)page[0x0B];
    return 0;
}

int iolink_master_apply_direct_parameter_page1(iolink_master_port_t* port,
                                               const uint8_t* page,
                                               uint8_t len)
{
    iolink_master_port_state_t* state;

    if(port == NULL)
    {
        return -1;
    }

    state = iolink_master_port_state(port);
    return iolink_master_parse_direct_parameter_page1(page, len, &state->device_info);
}

int iolink_master_get_device_info(const iolink_master_port_t* port,
                                  iolink_master_device_info_t* info)
{
    const iolink_master_port_state_t* state;

    if((port == NULL) || (info == NULL))
    {
        return -1;
    }

    state = iolink_master_port_const_state(port);
    *info = state->device_info;
    if(!info->valid)
    {
        return 1;
    }

    return 0;
}

int iolink_master_validate_device_info(const iolink_master_port_t* port)
{
    const iolink_master_port_state_t* state;
    const iolink_master_device_info_t* info;

    if(port == NULL)
    {
        return -1;
    }

    state = iolink_master_port_const_state(port);
    info = &state->device_info;
    if(!info->valid)
    {
        return 1;
    }

    if((info->revision_id != 0x10U) && (info->revision_id != 0x11U))
    {
        return -2;
    }

    if(state->config.min_cycle_time < info->min_cycle_time)
    {
        return -3;
    }

    if((state->config.pd_in_len != info->pd_in_len) ||
       (state->config.pd_out_len != info->pd_out_len))
    {
        return -4;
    }

    if(iolink_master_mseq_capability_code(state->config.m_seq_type) != info->operate_mseq_code)
    {
        return -5;
    }

    return 0;
}
