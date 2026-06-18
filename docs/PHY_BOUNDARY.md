# PHY Boundary

The master core is board-agnostic. Board support must live outside
`src/master_*.c` and enter the core only through `iolink_phy_api_t`,
`iolink_master_config_t`, and explicit time inputs such as
`iolink_master_tick_at()` or `iolink_master_controller_tick_at()`.

## Ownership

- The caller owns hardware timers and supplies monotonic 100us timestamps.
- The controller computes the next due timestamp with
  `iolink_master_controller_get_next_tick_time()`.
- The PHY adapter owns transceiver registers, UART/USART setup, C/Q line
  direction, fault pins, and board-specific interrupt wiring.
- The protocol core owns frame encoding/decoding, startup state, retry policy,
  service sequencing, process data, and diagnostics.

## Minimum PHY Operations

### IO-Link Mode

Required:

- `send`: transmit a complete encoded frame buffer.
- `recv_byte`: non-blocking byte receive from UART/USART.

Recommended:

- `set_mode`: switch the transceiver into SDCI mode.
- `set_baudrate`: apply COM1, COM2, or COM3 during fixed or auto-baud startup.
- `get_voltage_mv`: expose L+ diagnostics when the transceiver supports it.
- `is_short_circuit`: expose hard line faults when available.

### DI Mode

Required:

- `read_cq_line` in `iolink_master_config_t`.

Recommended:

- `set_mode`: switch the transceiver into SIO mode.

Not required:

- UART receive/transmit callbacks.

### DQ Mode

Required:

- `set_cq_line`: drive C/Q high or low.

Recommended:

- `set_mode`: switch the transceiver into SIO mode.

Not required:

- UART receive/transmit callbacks.

### Deactivated Mode

Recommended:

- `set_mode`: switch the transceiver into inactive/high-impedance mode.

## Adapter Rules

- Do not include board headers from `src/master_*.c`.
- Do not sleep inside core calls. Schedule the next call using the public next
  due-time helpers.
- Do not hide UART framing errors. Return a negative value from `recv_byte`.
- Do not partially report successful sends. `send` must return the exact length
  or a negative/short result so the core can enter error handling.
- Keep adapter fault policy explicit: line faults may be surfaced through PHY
  callbacks and public diagnostics, but must not mutate core state behind its
  back.
