# BMP280 — pressure and temperature

Driver for the Bosch BMP280 barometer. Reads pressure to about ±100 Pa
absolute (and rather better than that relative), plus a temperature channel
that exists mainly to compensate it.

| | |
|---|---|
| Header | `lib/bmp280/HBMP280/HBMP280.hpp` |
| Class | `HBMP280` |
| I2C address | `0x77` on this board, `0x76` if SDO is tied low |
| Bus speed | 400 kHz |
| Conversion time | 45 ms at the defaults, computed by `measureTimeMs()` |
| RAM | ~48 bytes per instance |

## Wiring

VDD (1.7–3.6 V — **not 5 V** unless the module has a regulator), GND, SCL, SDA,
and SDO which selects the address:

| SDO | Address |
|---|---|
| High / VDD | `0x77` ← the module on this device |
| Low / GND | `0x76` |

Most breakouts strap SDO for you. If nothing answers, try the other address —
`begin()` says which one to try.

CSB must be tied high for I2C. Every module of this kind does that on the board.

## Quick start

```cpp
#include "lib/bmp280/HBMP280/HBMP280.hpp"

static HBMP280 barometer(bus);

if (!barometer.begin()) {
  HWarning("no BMP280 at 0x%02X", HBMP280_ADDRESS);
}

HBMP280Sample sample;
if (barometer.measure(sample)) {
  HInfo("%.0f Pa (%.0f hPa), %.1f C",
        sample.pressurePa, sample.pressurePa / 100.0f, sample.temperatureC);
}
```

## API

### `HBMP280Sample`

| Field | Meaning |
|---|---|
| `float temperatureC` | Degrees Celsius, −40 to +85 |
| `float pressurePa` | Pascals, 30000 to 110000. Divide by 100 for hPa. |

### `HBMP280`

| Method | Blocks | Notes |
|---|---|---|
| `HBMP280(bus, address, speedHz)` | — | Stores the bus. Transmits nothing. |
| `bool begin(tOvs, pOvs, filter)` | ~30 ms | Identify, reset, read calibration, configure. |
| `bool isReady() const` | — | True once `begin()` has succeeded. |
| `bool measure(HBMP280Sample&)` | `measureTimeMs()` | Trigger, wait, read, compensate. |
| `bool startMeasurement()` | — | Trigger only. |
| `bool isBusy()` | — | One status byte. |
| `bool collect(HBMP280Sample&)` | — | Read and compensate the result. |
| `uint32_t measureTimeMs() const` | — | Conversion time at the current settings. |
| `bool reset()` | ~30 ms | Soft reset, then re-run `begin()`. |
| `static float altitudeM(pa, seaLevelPa)` | — | Barometric formula. See the caveat below. |

The sample is **written only on success**, so a failed read cannot put a stale
or zero number on a display.

### Settings

`begin()` takes three optional arguments. The defaults suit a room barometer:
pressure oversampled hard because it is the reading that matters, temperature
lightly because it is only an input to that, and a mild filter to keep doors
and draughts out of the trend.

```cpp
barometer.begin(HBMP280Oversampling::X2,    // temperature
                HBMP280Oversampling::X16,   // pressure
                HBMP280Filter::X4);         // IIR filter
```

| `HBMP280Oversampling` | Pressure noise | Conversion |
|---|---|---|
| `X1` | ~2.6 Pa | 7 ms |
| `X2` | ~2.0 Pa | 9 ms |
| `X4` | ~1.4 Pa | 14 ms |
| `X8` | ~1.0 Pa | 23 ms |
| `X16` | ~0.9 Pa | 45 ms |

(Conversion times are for that setting on pressure with temperature at `X2`.
`measureTimeMs()` computes the exact figure from the datasheet's formula, so
changing the settings cannot leave `measure()` reading a result that is not
there yet.)

`HBMP280Filter` — `Off`, `X2`, `X4`, `X8`, `X16` — is an internal IIR filter
that suppresses short spikes at the cost of settling over several samples. A
door slamming shifts indoor pressure enough to show up plainly in raw
readings. Use `Off` for a weather log where every sample must stand alone.

There is no "skipped" oversampling setting. The datasheet allows it; this
driver does not offer it, because pressure compensation takes the temperature
reading as an input and a skipped channel yields a wrong number rather than no
number.

## Forced mode, not normal mode

The sensor is left asleep and woken for one conversion at a time. Between
readings it draws well under a microamp. Normal mode — where the part
free-runs on its own timer — only pays off for a caller reading faster than
the standby period, and nothing here does.

Same blocking rule as the other drivers: `measure()` is for the main task or a
task of its own. For a scan-driven caller:

```cpp
barometer.startMeasurement();

// a later scan, at least measureTimeMs() after the trigger
if (!barometer.isBusy()) {
  barometer.collect(sample);
}
```

`isBusy()` returns **true when the sensor does not answer**, which keeps
`collect()` from being called on a dead bus.

## Configuration

| Macro | Default | Meaning |
|---|---|---|
| `HBMP280_ADDRESS` | `0x77` | `0x76` if SDO is tied low. |
| `HBMP280_I2C_SPEED_HZ` | `400000` | The part goes to 3.4 MHz; the shared bus does not. |
| `HBMP280_I2C_TIMEOUT_MS` | `100` | Per transfer. |
| `HBMP280_SEA_LEVEL_PA` | `101325.0f` | Altitude reference. |

## Things that will catch you

**Modules sold as "BMP280" are often BME280s.** Same address, same pinout, and
the listing frequently does not match the die. `begin()` reads the chip ID and
refuses anything that is not a BMP280, naming what it found instead:

| Chip ID | Part |
|---|---|
| `0x58` | BMP280 — what this driver speaks |
| `0x60` | BME280 — adds humidity, needs `ctrl_hum` written before it measures at all |
| `0x55` | BMP180 — older, different register map entirely |

A BME280 would otherwise appear to work here and quietly return stale
pressure. Supporting one is maybe an hour of work: temperature and pressure
are identical, and it adds a humidity channel, one register that must be
written *before* `ctrl_meas`, and nine more calibration coefficients.

**Its temperature is not the room's.** The die sits next to its own regulator
and reads a little high. Fine as a compensation input, worse than the AHT20 as
a thermometer. Prefer the AHT20 for anything shown to a person.

**Altitude is only as good as its reference.** With the default 101325 Pa,
`altitudeM()` is only as good as today's weather and can be tens of metres
out. Pass the local sea-level-adjusted pressure from a nearby station for
anything absolute. As a *relative* measure it is far better than that — the
error is nearly constant over minutes, so differences are trustworthy.

**Calibration is per-part.** Twenty-four bytes burned in at the factory, read
once by `begin()`. A raw BMP280 value is not a pressure in any unit until it
has been through them. If the block reads all-zeros or all-ones, `begin()`
fails rather than producing confident nonsense forever.

## Troubleshooting

| Symptom | Cause |
|---|---|
| `no sensor answering at 0x77` | Try `0x76`. Otherwise power, wiring, or pull-ups. |
| `chip ID 0x60 ... is not a BMP280` | It is a BME280. See above. |
| `calibration reads as 0x0000/0x0000` | Floating SDO, or a clone with an empty NVM. |
| Pressure plausible but drifting | Normal. Weather moves ~±2000 Pa; that is the signal. |
| Pressure jumps when a door shuts | Real. Raise the IIR filter. |
| Altitude wrong by tens of metres | The sea-level reference, not the sensor. |

## Registers, for reference

| Address | Register |
|---|---|
| `0x88`–`0x9F` | Calibration, 24 bytes, little-endian |
| `0xD0` | Chip ID |
| `0xE0` | Reset — write `0xB6` |
| `0xF3` | Status — bit 3 measuring, bit 0 NVM copy |
| `0xF4` | `ctrl_meas` — temp oversampling [7:5], pressure [4:2], mode [1:0] |
| `0xF5` | `config` — standby [7:5], filter [4:2], 3-wire SPI [0] |
| `0xF7`–`0xFC` | Result: pressure then temperature, MSB first, 20 bits each |

Compensation is Bosch's fixed-point arithmetic, transcribed from the datasheet
and deliberately not rearranged — the shifts are where the rounding happens.
The 64-bit pressure path is used rather than the 32-bit one, which trades
about 1 Pa of accuracy for a smaller integer type this core does not need.
Verified against the datasheet's reference vector: `t_fine` 128422 and 25.08 °C
exactly, pressure within 0.02 Pa of the quoted figure.
