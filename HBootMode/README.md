# HBootMode

Four disjoint firmwares share one binary. This module decides which of them the
current boot is running, and switches between them.

```cpp
#include <HBootMode/HBootMode.hpp>

switch (HBootMode::resolve()) {          // first thing app_main() does
  case HBootModeKind::Normal:       ... break;   // does the device's job
  case HBootModeKind::Configuring:  ... break;   // Wi-Fi AP + setup UI
  case HBootModeKind::Ota:          ... break;   // Wi-Fi STA + firmware pull
  case HBootModeKind::FactoryReset: ... break;   // erase config, boot Normal
}

HBootMode::rebootTo(HBootModeKind::Configuring);  // records + resets, never returns
```

`resolve()` runs once and caches; `current()` is the cheap read afterwards.
`requestOnce()` is `rebootTo()` without the reset, for when something else has to
happen first.

## How a request survives the reset

The requested mode is stored in two `RTC_NOINIT_ATTR` words — RTC slow memory
that the bootloader does **not** re-initialise from the image. It therefore
survives a software reset and deep sleep, but not a power cut, and a magic word
guards it because RTC memory powers up holding whatever the SRAM felt like:
without that check a factory-fresh device would have a one-in-four chance of
booting straight into an OTA client with nowhere to connect.

The request is consumed as it is read. That is what stops a firmware that
crashes in `Configuring` from looping there — the next boot is `Normal` again.

On desktop builds the two words are plain statics and `rebootTo()` calls
`std::exit(0)`: the process starts over from `main()`, and the request does not
outlive it, exactly as a power cycle would behave on the target.

## Not here yet

A **sticky** default — "this node is a setup kiosk until somebody says
otherwise" — needs a home in flash, which is HConfig's job. Until that module
exists, a boot with no pending request is `Normal`. When it lands, `resolve()`
grows a second branch (sticky value read from config) and gains a
`setDefault()`, with the one-shot request still winning over it.

`rebootTo()` waits `HBOOTMODE_UART_DRAIN_MS` (50, overridable in
`HCoreLibConfig.h`) for the console to finish shifting out before resetting, and
does **not** stop anything the application is doing — bring the device to a safe
state before calling it.
