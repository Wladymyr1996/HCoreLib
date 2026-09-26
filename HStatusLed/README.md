# HStatusLed

The family's status LED: one RGB LED saying what a device is doing, with
patterns that mean the same thing on every device.

## The standard patterns

| Status | Set with | Colour | Pattern |
| --- | --- | --- | --- |
| Bind: looking for a master | `setOverlay(BindSlave)` | yellow | 200 ms on / 200 ms off |
| Bind: master window open | `setOverlay(BindMaster)` | yellow | solid |
| Normal mode | `setBase(Normal)` | green | 50 ms on / 2 s off |
| Settings mode | `setBase(Configuring)` | yellow | 50 ms on / 2 s off |
| Factory reset | `setBase(FactoryReset)` | yellow | 50 ms on / 50 ms off |
| Failed | `setBase(Failed)` | red | 1 s on / 0.5 s off |
| Degraded | `setBase(Degraded)` | red | 50 ms on / 2 s off |

**Failed** means something the device needs did not start - the filesystem, the
radio, the portal. It is a base, so it replaces the mode's blink for the rest of
that boot; the log says what failed.

**Degraded** is Normal's heartbeat in red: the device runs, but something it
runs is not as configured - a relay controller with a relay on its failsafe,
say. The application switches between Normal and Degraded as it changes; it
never replaces Failed.

Defined once, in `HStatusLed.cpp`. A device picks which statuses it sets; it
does not invent its own meaning for a colour. Every cycle starts **lit**, so a
new status shows at once.

**Base and overlay.** The base is the boot mode and is always there. An overlay
is something happening for a while (a bind) and replaces the base while it is
set. Clearing it brings the base back. Setting a status the LED already shows
does not restart it.

## Enabling it

Off by default: not every board has the LED. In the application:

```c
// HCoreLibConfig.h
#define HSTATUSLED_ENABLE 1

// HGpioConfig.h - the one file that names a GPIO
#define HSTATUSLED_GPIO 8
```

With `HSTATUSLED_ENABLE` at 0, every call is an inline no-op and nothing is
linked, so an application calls it unconditionally and writes no `#if`.

| Define | Default | |
| --- | --- | --- |
| `HSTATUSLED_ENABLE` | `0` | drive the LED at all |
| `HSTATUSLED_GPIO` | — | the LED's data pad; required when enabled |
| `HSTATUSLED_BRIGHTNESS` | `16` | out of 255; every colour is scaled by it |
| `HSTATUSLED_COLOR_ORDER` | `HSTATUSLED_ORDER_GRB` | `HSTATUSLED_ORDER_RGB` if green shows as red and yellow as green (the HRelayController board's LED is one of these) |

## Using it

```cpp
HStatusLed::begin();                           // once, at start-up
HStatusLed::setBase(HStatusLedBase::Normal);   // from the boot mode

// in the owning task's loop, every HCORELIB_TICK_MS
HStatusLed::update();

HStatusLed::setOverlay(HStatusLedOverlay::BindSlave);  // a bind started
HStatusLed::clearOverlay();                             // ...and ended

HStatusLed::runFor(1000);  // a mode with no loop: tick for 1 s, blocking
```

**Ticked by the owning task, not by a timer.** The Normal blip is therefore a
sign of life: a wedged loop stops blinking. Everything is called from one task,
with no lock.

## Inside

| File | |
| --- | --- |
| `HStatusLed` | the table, base/overlay, and `tick(now)`; platform-independent |
| `HIRgbLed` | the backend interface: `begin()`, `show(r, g, b)` |
| `HRgbLedEsp32` | a WS2812-type LED on an RMT TX channel (10 MHz, bytes encoder). The channel is allocated in `begin()`, and nothing after it |
| `HRgbLedDesktop` | three bytes and a write count, for host tests |

The LED is written only when its colour changes. `HStatusLedTest.cpp` checks
each pattern at its exact edges, the overlay rules, and the clock wrap.

The WS2812's data input is on a strapping pad on some boards (GPIO8 on the
ESP32-C6). The LED does not pull it low, so booting is unaffected. Keep that in
mind before wiring anything else to the pad.
