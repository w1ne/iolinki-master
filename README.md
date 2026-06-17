# iolinki-master

`iolinki-master` is a standalone IO-Link master stack. It is intentionally split
from the device-oriented `iolinki` repository and reuses only the narrow shared
pieces needed for CRC, frame handling, PHY contracts, and IO-Link constants.

The master API is built around caller-owned opaque storage. Public users allocate
`iolink_master_port_t` or `iolink_master_controller_t`; private state lives in
`src/` and is not exposed through the public header.

Track implementation status and next work here:

- [`docs/IMPLEMENTATION_STATUS.md`](docs/IMPLEMENTATION_STATUS.md)
- [`docs/ROADMAP.md`](docs/ROADMAP.md)

The default local dependency path is:

```sh
/home/andrii/projects/labwired/core/third_party/iolinki
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

`iolink_master_get_pd_in()` returns `-1` for invalid arguments, `-2` when the
caller buffer is too small, `1` when process data is not valid yet, and `0` when
valid data was copied. For `-2` and `1`, `out_len` is set to the required
process-data length.

Several public APIs still use integer status codes. Converting those to named
public result constants is the next API-hardening slice.

To point at another local `iolinki` checkout:

```sh
cmake -S . -B build -DIOLINKI_DEVICE_DIR=/path/to/iolinki
```
