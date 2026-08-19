# HGpioManager

Pins, by name, with inversion already applied.

```cpp
HGpioManager::configureAll();                    // once, early in app_main
const HGpioPin button = HGpioManager::find("btn");
if (button.read()) { ... }                       // true means PRESSED
```

No task, no interrupt, no state. Reading a pad is a register read, so code that
must notice a *change* polls it from `update()` (see [HITickable](../HITickable/))
and measures with [HTimer](../Utils/HTimer/). An edge interrupt would buy nothing
for a human-speed input and would drag every consumer into ISR rules — no
logging, no flash, no blocking — for a signal that bounces twenty times per press
anyway. Per-pin `attachInterrupt()` can be added later for the things that
genuinely need it: encoders, pulse counting, wake-from-sleep.

## The board is declared, not discovered

Every pin comes from `App/Config/HGpioConfig.h`, which the **application** owns
and which is the only file in the firmware naming a GPIO number:

```c
#define HGPIO_FIXED_PINS(PIN)                 \
  /*  name,  gpio,  dir,    pull,  invert */  \
  PIN("btn",    2,  Input,  Up,    true)
```

The table's length *is* its capacity — no `HGPIO_MAX_PINS` to outgrow, and a
board with one pin carries storage for one pin. Three mistakes fail the **build**
rather than the boot: a number this chip does not have, an `Output` on a pad that
cannot drive, and the same pad claimed by two names.

Second list, `HGPIO_CONFIGURABLE_PINS`, is for pads an installer may repurpose —
their direction and inversion are meant to come from `config/gpio.cfg` through
HConfig. This board declares none, so nothing reads that file yet; the split
exists so the next board does not have to reshape the table.

## Two things stop at this boundary

**Numbers.** Above `find()` a pin is `"btn"`, so moving the button to another pad
is one line in the board table.

**Inversion.** `HGpioPin::read()` and `write()` apply `invert` in both
directions, so a value crossing that class is always what it *means*. A switch to
ground behind a pull-up is one `true` in the table, and HButton contains not a
word about wiring.

An unknown name gives back an **invalid** handle that reads `false` and drives
nothing — a typo costs a pin that never acts, not a crash — and is logged.

## Layout

| | |
| --- | --- |
| `HGpioManager` | the static facade: `configureAll`, `find`, `pinCount`, `at` |
| `HGpioPin/` | the handle callers hold; where inversion is applied |
| `HGpioTypes/` | `HGpioDir`, `HGpioPull`, `HGpioPinDesc` — the row format |
| `HIGpio/` | pad-level contract: configure, `readRaw`, `writeRaw` |
| `HGpioEsp32/` | target backend, on `gpio_config()` |
| `HGpioDesktop/` | host backend: an array of levels, with `setRawLevel()` so tests can move a wire |
