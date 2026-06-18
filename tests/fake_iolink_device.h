#ifndef IOLINKI_MASTER_TESTS_FAKE_IOLINK_DEVICE_H
#define IOLINKI_MASTER_TESTS_FAKE_IOLINK_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "iolinki/phy.h"

void fake_iolink_device_reset(uint8_t pd_in_value, uint8_t pd_in_len, uint8_t od_len);
void fake_iolink_device_set_isdu_object(uint16_t index, uint8_t subindex, const uint8_t* data, uint8_t len);
void fake_iolink_device_set_event_pending(bool pending);
const iolink_phy_api_t* fake_iolink_device_phy(void);
uint32_t fake_iolink_device_wakeup_count(void);
uint32_t fake_iolink_device_transition_count(void);
uint32_t fake_iolink_device_operate_cycle_count(void);

#endif /* IOLINKI_MASTER_TESTS_FAKE_IOLINK_DEVICE_H */
