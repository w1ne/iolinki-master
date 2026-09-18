# Changelog

All notable changes to the `iolinki-master` project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed
- **BREAKING: spec-conformant wire (IO-Link V1.1.5).** The master now speaks the
  spec message checksum (A.1.6), reply layout (A.1.5: `[PD-in][OD] CKS`, no
  status octet), ISDU transport with Length/ExtLength/CHKPDU and FlowCTRL
  segmentation (7.3.6, A.5, Table 52), events over the DIAGNOSIS channel
  (7.3.8, Table 58/59), Table A.10 OPERATE M-sequence codes, Table B.6
  descriptor validation, and the C6 timing rules. This is wire-incompatible
  with every prior release. `IOLINK_MASTER_PORT_STORAGE_SIZE` grew from 1280 to
  1296 bytes to hold the extra timing state.
- **Magic numbers extracted to named constants** (`src/master_internal.h`): the
  retry budget, wake-up byte, frame buffer size, Direct Parameter Page 1 field
  offsets, MinCycleTime / M-sequenceCapability / ProcessData-descriptor bit
  fields, ISDU framing lengths, Data Storage record and event-entry sizes, and
  the startup micro-sequence steps are now named rather than inline literals.
- **MISRA C:2012 pass**: fixed Rule 17.7 (ignored `memcpy`/`memset` returns now
  `(void)`-cast), Rule 13.4 (assignment-in-`while` → explicit read + `break`),
  and Rule 15.7 (all `if … else if` chains terminated). Remaining accepted
  deviations are recorded in [`docs/MISRA_DEVIATIONS.md`](docs/MISRA_DEVIATIONS.md);
  `check_quality.sh` now also finds the Debian x86_64 cppcheck MISRA addon path.

### Added
- **Startup timing config**: `t_dmt_tbit` (T_DMT, default 32 bit times) and
  `t_dwu_100us` (T_DWU, default 400 = 40 ms); `wake_retry_limit` now defaults to
  the spec n_WU = 2 when 0, and the response deadline is floored at (11 + 10)
  T_BIT per A.3.5/A.3.6.

### Fixed
- **ProcessData descriptor decode** (`master_parameters.c`): isolate the Length
  field to bits 0-4 per Table B.6 so a device that sets the (legal) SIO bit, or
  reports a sub-byte bit length, is decoded to the correct octet count.
- **Retry recovery** (`master_port.c`): after RX retry exhaustion the port
  returns to STARTUP and re-issues a wake-up (7.2.2.1) instead of latching
  ERROR; ERROR stays reserved for PHY failures.
- **Startup probe** (`master_port.c`): the MinCycleTime probe octet is stored in
  `device_info.min_cycle_time` under every inspection level, including
  `NO_CHECK`.

### Documented
- **Spec conformance audit** against Interface & System Spec V1.1.5 in
  `docs/IMPLEMENTATION_STATUS.md`: the wire, ISDU transport, events, codes and
  timing are now conformant for the master slice (C1..C6); the former
  startup/OPERATE-transition and ISDU I-Service deviations are gone.

## [0.2.0] - 2026-07-04

Release-engineering and documentation parity with the sibling `iolinki` device
stack. No protocol behavior changes beyond one guarded cast.

### Added
- **Security package**: STRIDE [threat model](docs/security/THREAT_MODEL.md)
  aligned to the IO-Link Security Design and Development Guideline (Order No.
  10.512) and an EU Cyber Resilience Act overview ([CRA.md](docs/security/CRA.md)).
- **Per-release SBOMs**: `tools/generate_sbom.py` emits reproducible CycloneDX 1.6
  and SPDX 2.3 documents (self-tested by `tools/test_generate_sbom.py`, gated in
  CI via the `sbom-tools` job) and attaches them to each tagged release.
- **Release automation**: `.github/workflows/release.yml` builds with coverage,
  runs tests, generates notes, and publishes a GitHub Release with SBOMs on `v*`.
- **Local quality gate**: `check_quality.sh` (strict `-Werror -Wpedantic
  -Wconversion -Wshadow` compile, cppcheck, opt-in MISRA C:2012, clang-format,
  Doxygen zero-warning) and `tools/run-cppcheck.sh`.
- **Developer tooling**: `.pre-commit-config.yaml`, `ENABLE_COVERAGE` and
  `IOLINK_MASTER_ENABLE_DOCS` CMake options, and a `Doxyfile`.
- **Documentation**: `docs/ARCHITECTURE.md`, `docs/API.md`, `docs/PORTING.md`,
  `docs/CONTRIBUTING.md`, and `docs/RELEASE_STRATEGY.md`.

### Fixed
- Explicit cast on the ISDU read out-length (`master_isdu.c`) to satisfy
  `-Wconversion`; the value is already bounds-checked, so behavior is unchanged.

## [0.1.0] - 2026-07-04

First tagged release. `iolinki-master` is a portable, heap-free IO-Link **master**
protocol library with a caller-owned public API, validated against a co-designed
simulated device and an on-wire firmware model — **not yet a conformant master for
real hardware** (see [`docs/IMPLEMENTATION_STATUS.md`](docs/IMPLEMENTATION_STATUS.md)
for the honest gap list).

### Added
- **Port lifecycle and startup**: inactive/startup/preoperate/operate/error state
  machine, wake-up + Type 0 idle + operate transition, fixed and auto-baudrate
  (COM3→COM2→COM1) scan, and configurable per-baud wake-up retry.
- **Cyclic process data**: configured PD in/out for M-sequence Types 0, 1_1/1_2/1_V,
  and 2_1/2_2/2_V, with RX accumulation, checksum handling, and retry tracking.
- **ISDU services**: read/write with segmentation, Data Storage read/write/restore
  with readback verification, block parameterization (download/upload/store), and
  ISDU readback verification.
- **Device identity**: Direct Parameter Page 1 parse/apply/validate, capability-driven
  config selection, and `NO_CHECK`/`TYPE_COMP`/`IDENTICAL` inspection levels enforcing
  VendorID/DeviceID at startup.
- **MasterCycleTime decoding**: the time-base/multiplier octet is decoded to 100us
  units for validation and cycle pacing.
- **Master Command helpers**: R/W + communication-channel + address encode/decode.
- **Events**: event-code/detail read and ack, plus optional dispatch callbacks
  (rising-edge event-pending notify and per-event handler).
- **Scheduler/timing**: monotonic 100us cycle pacing, response-deadline scheduling,
  and cycle-slip / jitter / link-quality diagnostics.
- **Multi-port controller**: init/tick fan-out with per-port pacing and diagnostics.
- **SIO DI/DQ** modes and dynamic mode transitions.
- **PHY boundary**: hardware-independent protocol core with a documented PHY contract
  and adapter hooks (checked mode/baudrate, RX flush, half-duplex TX/RX prepare).
- **Tooling and tests**: CMake build against the sibling `iolinki` frame/CRC helpers,
  16 CTest targets (CMocka), a fake-device harness, runnable 1-port and 4-port
  examples, and CI (`cmake-ctest` + the on-wire `labwired-real-firmware-model`).
- **Project baseline**: dual-license model (GPLv3 + commercial), coordinated-disclosure
  `SECURITY.md`, and `.clang-format` / `.editorconfig`.

[Unreleased]: https://github.com/w1ne/iolinki-master/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/w1ne/iolinki-master/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/w1ne/iolinki-master/releases/tag/v0.1.0
