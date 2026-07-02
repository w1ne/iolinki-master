# iolinki-master

`iolinki-master` is a standalone IO-Link master stack. It is intentionally split
from the device-oriented [`iolinki`](https://github.com/w1ne/iolinki) repository
and reuses only the narrow shared pieces needed for CRC, frame handling, PHY
contracts, and IO-Link constants.

The master API is built around caller-owned opaque storage. Public users allocate
`iolink_master_port_t` or `iolink_master_controller_t`; private state lives in
`src/` and is not exposed through the public header.

Track implementation status and next work here:

- [`docs/IMPLEMENTATION_STATUS.md`](docs/IMPLEMENTATION_STATUS.md)
- [`docs/ROADMAP.md`](docs/ROADMAP.md)
- [`docs/TESTING.md`](docs/TESTING.md)

The build needs a local checkout of the `iolinki` device repository for the
shared CRC/frame helpers (and, for tests, the real device stack). By default it
is expected as a sibling of this repository:

```sh
git clone -b develop git@github.com:w1ne/iolinki.git ../iolinki
```

Build and test:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The build compiles only the narrow shared helper sources from the local `iolinki`
checkout into the master build. It should not link or expose the full device
stack.

Public APIs return named integer-compatible result constants such as
`IOLINK_MASTER_STATUS_OK`, `IOLINK_MASTER_STATUS_PENDING`, and
`IOLINK_MASTER_ERR_INVALID_ARG`.

Runnable examples are built by default:

- `master_loopback_demo`: one IO-Link port startup and cyclic process-data flow.
- `master_4port_controller_demo`: mixed 4-port controller setup with IO-Link,
  DI, DQ, and deactivated ports.

To point at another local `iolinki` checkout:

```sh
cmake -S . -B build -DIOLINKI_DEVICE_DIR=/path/to/iolinki
```

## Related Projects

- **[iolinki](https://github.com/w1ne/iolinki)** — the companion IO-Link
  **device** stack. This repository builds against it for the shared
  CRC/frame/PHY helpers, and CI runs both stacks against each other over a
  simulated wire (real firmware, multi-port station) in LabWired.

## Security

The security and CRA documentation for the iolinki project family lives in the
device repository and covers the shared frame/CRC layer this stack reuses:

- [SECURITY.md](https://github.com/w1ne/iolinki/blob/develop/SECURITY.md) —
  coordinated disclosure policy (applies to this repository as well; use
  GitHub private vulnerability reporting here or there)
- [Threat model](https://github.com/w1ne/iolinki/blob/develop/docs/security/THREAT_MODEL.md)
  and [CRA overview](https://github.com/w1ne/iolinki/blob/develop/docs/security/CRA.md)

A master-stack-specific threat model and per-release SBOMs will follow once
this repository starts tagging releases.

## License

`iolinki-master` follows the same licensing model as
[`iolinki`](https://github.com/w1ne/iolinki): dual-licensed under the **GPLv3**
(free, for open-source/GPLv3 use) and a **commercial license** (for
closed-source / proprietary products that cannot accept the GPLv3 copyleft).
Shipping a proprietary product? Email **andrii@shylenko.com**. See
[`LICENSE`](LICENSE) and [`LICENSE.COMMERCIAL`](LICENSE.COMMERCIAL).
