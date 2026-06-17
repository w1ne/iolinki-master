#ifndef IOLINKI_MASTER_TESTS_FAKE_IOLINK_DEVICE_H
#define IOLINKI_MASTER_TESTS_FAKE_IOLINK_DEVICE_H

#include <stdint.h>

#include "iolinki/phy.h"

void fake_iolink_device_reset(uint8_t pd_in_value, uint8_t pd_in_len, uint8_t od_len);
const iolink_phy_api_t* fake_iolink_device_phy(void);
uint32_t fake_iolink_device_wakeup_count(void);
uint32_t fake_iolink_device_transition_count(void);
uint32_t fake_iolink_device_operate_cycle_count(void);

#endif /* IOLINKI_MASTER_TESTS_FAKE_IOLINK_DEVICE_H */
