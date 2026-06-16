# iolinki-master

`iolinki-master` is a separate IO-Link Master skeleton. It is intentionally split
from the `iolinki` device repository and consumes that local dependency for
shared CRC, protocol, PHY, and frame helpers.

The default local dependency path is:

```sh
/home/andrii/projects/labwired/core/third_party/iolinki
```

Build and test:

```sh
cmake -S . -B build
cmake --build build --target test_master_startup
./build/tests/test_master_startup
```

When consumed through CMake, this repository forces the local `iolinki`
dependency tests off and adds the dependency with `EXCLUDE_FROM_ALL`. The
current dependency CMake still configures example targets internally, but they
are not part of the master library or startup test build.

`iolink_master_get_pd_in()` returns `-1` for invalid arguments, `-2` when the
caller buffer is too small, `1` when process data is not valid yet, and `0` when
valid data was copied. For `-2` and `1`, `out_len` is set to the required
process-data length.

To point at another local `iolinki` checkout:

```sh
cmake -S . -B build -DIOLINKI_DEVICE_DIR=/path/to/iolinki
```
