# Hardware Validation Matrix

Local CTests verify protocol behavior only. A hardware-tested master needs the
matrix below before claiming real-device support.

## Required Setup

- One IO-Link master PHY adapter wired through `iolink_phy_api_t`.
- One known sensor with cyclic PD input.
- One known actuator or output module with cyclic PD output.
- Capture path for UART frames or a logic analyzer on the PHY side.
- Monotonic timer source feeding `iolink_master_controller_tick_at()`.

## Matrix

| Area | Sensor | Actuator | Evidence |
| --- | --- | --- | --- |
| Startup | Wake-up, baudrate, PREOPERATE, OPERATE | Wake-up, baudrate, PREOPERATE, OPERATE | Captured startup frames and final state |
| Cycle timing | Min cycle respected for 10k cycles | Min cycle respected for 10k cycles | Timing log with max jitter and slips |
| PD input | PD valid and stable under nominal operation | Status input if available | Captured PD bytes and API readback |
| PD output | Not applicable unless sensor accepts output | Output command reflected by device | Captured master frame and device behavior |
| ISDU read | Vendor ID, Device ID, status objects | Vendor ID, Device ID, status objects | API result and captured ISDU frames |
| ISDU write | Application tag or safe writable object | Application tag or safe writable object | Write result and readback |
| Events | Trigger or simulate one event | Trigger or simulate one event | Event code/details and ack behavior |
| Data Storage | Backup/read object where supported | Backup/read/restore where supported | Readback and restore evidence |
| Faults | Disconnect/CRC/no-response handling | Disconnect/CRC/no-response handling | Diagnostics counters and recovery/error state |
| Soak | At least 8 hours cyclic read | At least 8 hours cyclic read/write | Error counters, link quality, timing stats |

## Pass Criteria

- No unexpected error state during nominal startup and cyclic operation.
- Captured frames match the configured M-sequence type, PD sizes, OD size, and
  checksum expectations.
- Public diagnostics show bounded jitter, no unexplained checksum growth, and
  link quality consistent with captured faults.
- Service APIs return `OK`, `PENDING`, or documented negative result codes only.
- Any hardware-specific behavior is isolated in the adapter, not in
  `src/master_*.c`.

## What This Does Not Prove

This matrix is not official IO-Link master conformance. Official conformance
testing remains a separate external validation step.
