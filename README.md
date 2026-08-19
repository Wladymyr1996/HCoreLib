# HCoreLib

The Hatynka firmware library: the parts every device in the ecosystem needs, so
that a new one is a board definition and an application rather than another
copy of the same infrastructure.

Written for ESP-IDF on ESP32 targets, and deliberately buildable on a desktop —
the logic that can be tested without hardware is tested without hardware.

## The rules it is built to

- **No heap after start-up.** Every container is ETL with a fixed capacity, and a
  capacity that can be outgrown says so rather than growing.
- **No exceptions, no RTTI.** A failure is a return value or a log line.
- **Platform behind one seam.** `IS_MCU` / `IS_DESKTOP` from `HCoreLib.h`, with
  exactly one backend compiled in — no runtime switching and no dead platform
  code in the image.
- **The library never depends on the application.** What only a device knows
  arrives as a config macro, an argument, or a hook.

## What is in it

The whole of it on one page, classes and relations only, with each one marked
boundary / control / entity: [Docs/ClassDiagram.puml](Docs/ClassDiagram.puml).

### Core — `HCoreLib` component

| Module | Does |
| --- | --- |
| `HLog` | levelled logging with pluggable backends; console backend built in |
| `HConfig` | crash-safe configuration files, O(1) memory in file size |
| `HValue` | a strictly-typed, heap-free scalar: null, bool, int, float, string |
| `HJson` | a mutable JSON DOM carved out of a caller-owned buffer |
| `HFs` | filesystem behind one interface — LittleFS on target, real files on host |
| `HGpioManager` | pins by NAME, with inversion applied at the pin edge |
| `HButton` | debounce, press / long-press / release, on a logical pin |
| `HTimer` | one-shot timeouts against an absolute, wrap-safe clock |
| `HTask` | a FreeRTOS task as an object, with watchdog enrolment built in |
| `HTaskManager` | liveness watchdog: a task that stops reporting restarts the device cold |
| `HRtcStore` | the magic-word dance every block of retained memory needs |
| `HHookList` | fixed-capacity callback lists — how a library invites an app in |
| `HITickable` | the `update()` contract: called every `HCORELIB_TICK_MS`, must not block |
| `HBootMode` | four disjoint firmwares in one binary, selected through RTC memory |
| `HSleep` | deep sleep, wake sources, and the hooks that run before and after |
| `HAuth` | admin password (hashed) and single-session keys |
| `HSha256` | SHA-256 with no dependencies, so the same code runs in a host test |
| `HSystemUtils` | the millisecond clock, sleeping, and log decoration |

### `HCoreLib/Devices` — I2C peripherals

`HAHT20` (temperature, humidity), `HBMP280` (pressure), `HSSD1306` (monochrome
OLED, a template on panel size, with 6×8 / 8×16 / 24×32 fonts covering Latin,
Cyrillic and Ukrainian). Both sensors expose a blocking `measure()` and a
non-blocking `startMeasurement()`/`collect()` split.

Target-only: everything here speaks to a bus.

### `HCoreLib/Portal` — the settings mode

A network, a web server, a REST API, a page, and a way back to factory defaults —
identical on every device, with the device-specific parts injected. See
[Portal/README.md](Portal/README.md).

Its own component, so a device that serves no portal builds no Wi-Fi, no HTTP
and no mDNS.

## What an application must provide

Two headers, named by the library and owned by the application:

| File | Holds |
| --- | --- |
| `HCoreLibConfig.h` | overrides for any library limit — buffer sizes, timeouts, the network's identity |
| `HGpioConfig.h` | the board's pin table: name, GPIO, direction, pull, inversion |

Their directory is passed to CMake as `HCORELIB_CONFIG_DIR`. Every tunable has a
default declared next to the code that consumes it, so an application only names
what it disagrees with — and a missing pin table fails the build rather than the
boot.

## Using it

```cmake
set(HCORELIB_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/App/Config" CACHE INTERNAL "")

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/HCoreLib"
    "${CMAKE_CURRENT_LIST_DIR}/HCoreLib/Devices"   # only if you need the drivers
    "${CMAKE_CURRENT_LIST_DIR}/HCoreLib/Portal")   # only if you need the portal
```

Then `REQUIRES HCoreLib` — plus `Devices` or `Portal` where a component actually
needs them.

## On the desktop

HCoreLib builds as a plain static library with CMake, with `HFs` on real files and
`HGpioManager` on an array of levels a test can drive. That is what makes the
debounce state machine, the config round trip, the JSON DOM and the password
hashing testable on a machine with no board attached — running the same code the
device runs, not a reimplementation of it.

### Tests

[Tests/](Tests/) is that, done: a standalone CMake project with no dependencies of
its own, covering every module the host build compiles — `HValue`, `HJson`,
`HConfig` (paths, the on-disk format, patching, crash recovery), `HLog`,
`HSha256` against the published vectors, `HTimer`, `HHookList`, `HFs`,
`HGpioManager`, `HButton` through a synthetic bounce train, `HRtcStore` and
`HAuth`.

```
cmake -S Tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

The same three commands run in CI on Linux and Windows, in Debug and Release —
see [.github/workflows/tests.yml](.github/workflows/tests.yml). `HTask`,
`HTaskManager`, `Devices/` and `Portal/` are target-only and are not built here,
so they have no suite; the firmware repositories that consume this one cover them.

## Tools

`tools/packui.py` folds a web UI's HTML, CSS and JS into one gzipped file for
embedding — see [Portal/README.md](Portal/README.md).

## Dependencies

[ETL](https://www.etlcpp.com/) for containers - header-only, so HCoreLib simply adds
its include path and its own `EtlProfile/etl_profile.h`; there is no component
and nothing to link. The portal additionally pulls `espressif/mdns`; `HFs` on target pulls
`joltwallet/littlefs`. Nothing else.
