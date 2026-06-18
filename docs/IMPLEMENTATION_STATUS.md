# iolinki-master Implementation Status

This file is the living implementation ledger for the master stack. It should be
updated when a feature graduates from open to partial, or from partial to
implemented.

Status definitions:

- Implemented: code exists and local tests cover the intended behavior.
- Partial: useful code exists, but the standard-facing behavior, API contract,
  or test coverage is incomplete.
- Open: no meaningful implementation yet.

## Status Matrix

| Area | Status | Evidence | Remaining Gap |
| --- | --- | --- | --- |
| Repository boundary | Implemented | [`CMakeLists.txt`](../CMakeLists.txt), [`README.md`](../README.md) | Keep avoiding full device-stack linkage as new shared helpers are needed. |
| Public API shape | Partial | [`include/iolinki_master/master.h`](../include/iolinki_master/master.h), [`tests/test_master_public_header.c`](../tests/test_master_public_header.c) | Tune storage-size rationale and add more black-box tests that avoid private state. |
| Opaque storage/private state | Implemented | [`include/iolinki_master/master.h`](../include/iolinki_master/master.h), [`src/master_internal.h`](../src/master_internal.h) | Tune the public storage sizes once the private state stops moving quickly. |
| Port lifecycle | Implemented | [`src/master_port.c`](../src/master_port.c), [`tests/test_master_startup.c`](../tests/test_master_startup.c) | Add a public lifecycle example for downstream users. |
| Startup and baudrate scan | Implemented | [`src/master_port.c`](../src/master_port.c), [`tests/test_master_startup.c`](../tests/test_master_startup.c) | Validate timing and retries against real hardware. |
| M-sequence handling | Partial | [`src/master_port.c`](../src/master_port.c), [`tests/test_master_pd.c`](../tests/test_master_pd.c), [`tests/test_master_startup.c`](../tests/test_master_startup.c) | Add device-capability negotiation and full variant coverage. |
| Cyclic process data | Implemented | [`src/master_port.c`](../src/master_port.c), [`tests/test_master_pd.c`](../tests/test_master_pd.c), [`tests/test_master_public_flow.c`](../tests/test_master_public_flow.c) | Add more black-box coverage for configured PD sizes and invalid user buffers. |
| RX path and retries | Implemented | [`src/master_port.c`](../src/master_port.c), [`tests/test_master_startup.c`](../tests/test_master_startup.c), [`tests/test_master_tick.c`](../tests/test_master_tick.c) | Add line-noise and long-running soak tests with a real PHY. |
| ISDU read/write | Partial | [`src/master_isdu.c`](../src/master_isdu.c), [`tests/test_master_isdu.c`](../tests/test_master_isdu.c) | Add public black-box ISDU tests and verify behavior against real devices. |
| Direct Parameter Page 1 | Implemented | [`src/master_parameters.c`](../src/master_parameters.c), [`tests/test_master_parameters.c`](../tests/test_master_parameters.c), [`tests/test_master_isdu.c`](../tests/test_master_isdu.c) | Use parsed capability data for automatic master configuration. |
| Startup device validation | Implemented | [`src/master_parameters.c`](../src/master_parameters.c), [`src/master_port.c`](../src/master_port.c), [`tests/test_master_startup.c`](../tests/test_master_startup.c) | Expand validation once automatic negotiation exists. |
| Diagnostics | Partial | [`include/iolinki_master/master.h`](../include/iolinki_master/master.h), [`src/master_port.c`](../src/master_port.c), [`tests/test_master_pd.c`](../tests/test_master_pd.c) | Add event detail, link-quality metrics, and stable public error taxonomy. |
| Multi-port controller | Partial | [`src/master_controller.c`](../src/master_controller.c), [`tests/test_master_controller.c`](../tests/test_master_controller.c) | Add scheduler ownership, port-level timing policy, and examples. |
| SIO DQ | Partial | [`src/master_sio.c`](../src/master_sio.c), [`tests/test_master_startup.c`](../tests/test_master_startup.c) | Add DI input and dynamic mode-transition behavior. |
| Scheduler/timing | Partial | [`src/master_port.c`](../src/master_port.c), [`src/master_controller.c`](../src/master_controller.c), [`tests/test_master_tick.c`](../tests/test_master_tick.c), [`tests/test_master_controller.c`](../tests/test_master_controller.c) | Implement min-cycle-time pacing, timer integration, and jitter/error accounting. |
| Events | Open | [`include/iolinki_master/master.h`](../include/iolinki_master/master.h) | Implement event read/ack and expose decoded event detail. |
| Data Storage | Open | None | Add master-side parameter storage and restore behavior. |
| Block parameterization | Open | None | Add block write/readback workflows and verification policy. |
| Hardware PHY adapters | Open | [`include/iolinki_master/master.h`](../include/iolinki_master/master.h) consumes the dependency PHY contract | Add real master-port hardware adapters outside the protocol core. |
| Conformance | Open | Local tests only | Run official IO-Link master conformance testing. |
| Documentation/examples | Partial | [`README.md`](../README.md), [`docs/ROADMAP.md`](ROADMAP.md), [`docs/TESTING.md`](TESTING.md) | Add focused examples for startup, cyclic PD, ISDU, SIO, and controller use. |

## Checkable Ledger

Use this section for quick progress checks. The table above keeps the evidence
and gap detail.

### Done

- [x] Separate master repository/build from the device stack.
- [x] Compile only narrow shared helper sources from the local `iolinki` checkout.
- [x] Public opaque caller-owned port/controller storage.
- [x] Public named result codes and documented function return contracts.
- [x] Private master state under `src/`.
- [x] Port lifecycle states: inactive, startup, preoperate, operate, error.
- [x] Startup wake-up, Type 0 idle, transition command, and operate entry.
- [x] Fixed-baudrate startup.
- [x] Auto-baudrate scan across COM3/COM2/COM1.
- [x] Configured cyclic PD input/output.
- [x] RX accumulation, checksum handling, and retry tracking.
- [x] ISDU read/write transfer in local tests.
- [x] Direct Parameter Page 1 parse/apply/get/validate.
- [x] Optional startup device-info validation.
- [x] Basic diagnostics API.
- [x] Response timeout counter in public diagnostics.
- [x] Cycle-slip counter in public diagnostics.
- [x] Multi-port controller init/tick helper.
- [x] Event-driven tick dispatch for none, cycle-due, and response-timeout events.
- [x] Port-level `min_cycle_time` pacing with fake monotonic 100us ticks.
- [x] Per-port controller tick events.
- [x] Controller time-aware tick fan-out for per-port cycle pacing.
- [x] Controller-owned response-deadline timeout scheduling across ports.
- [x] SIO DQ output through `set_cq_line`.
- [x] Public header compile test.
- [x] Public black-box startup/process-data flow test.
- [x] Fake-device harness for startup, transition, cyclic PD, and port pacing.

### In Progress

- [ ] Add more public black-box tests for ISDU and SIO.
- [ ] Tune or justify public opaque storage sizes.
- [ ] Complete M-sequence variant coverage.
- [ ] Expand diagnostics into a stable master health model.
- [ ] Add controller examples and clearer multi-port runtime policy.

### Not Started

- [ ] Full scheduler/timing model with jitter diagnostics.
- [ ] Capability-matrix fake devices.
- [ ] Fake-device ISDU object dictionary.
- [ ] Capability-driven M-sequence and PD-size selection.
- [ ] DI input API/PHY support.
- [ ] Dynamic SIO/IO-Link mode transitions.
- [ ] Event read/ack and event-detail decoding.
- [ ] Data Storage / parameter server behavior.
- [ ] Block parameterization and readback verification.
- [ ] Expand fake-device harness into a conformance-style matrix.
- [ ] Real hardware PHY adapter.
- [ ] Real-device sensor/actuator test matrix.
- [ ] Official IO-Link master conformance validation.

## Current Test Targets

Local CTest currently exercises these targets when CMocka is available:

- `test_master_startup`
- `test_master_pd`
- `test_master_isdu`
- `test_master_tick`
- `test_master_controller`
- `test_master_parameters`
- `test_master_public_flow`
- `test_master_public_header`
- `master_loopback_demo`
- `test_master_fake_device`

Use this verification loop before committing master-stack changes:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

## Documentation Rules

Update this file in the same commit as implementation changes when the status of
a feature changes. Keep the gap column honest: passing local tests does not mean
hardware or conformance coverage exists.

## Architecture Priority

Do not treat all open rows as equal. The scheduler/timing row is the current
architecture blocker: without an explicit cycle/deadline model, the master is a
protocol engine that can be driven by tests, not yet a complete embedded master
runtime.
