# AHT20 — temperature and humidity

Driver for the ASAIR AHT20, the small four-pin humidity sensor. Reads
temperature to about ±0.3 °C and relative humidity to about ±2 %.

| | |
|---|---|
| Header | `lib/aht20/HAHT20/HAHT20.hpp` |
| Class | `HAHT20` |
| I2C address | `0x38`, fixed |
| Bus speed | 400 kHz |
| Conversion time | 80 ms |
| RAM | ~24 bytes per instance |

## Wiring

Four pins: VDD (2.2–5.5 V), GND, SCL, SDA. The part has **no address pins**, so
exactly one AHT20 can live on a bus — a second one needs a second bus or an I2C
multiplexer.

It shares the bus with the display and the barometer. On this board that is
`I2C_NUM_0`, SDA on GPIO1 and SCL on GPIO0 — see `openBus()` in `App/main.cpp`.

## Quick start

```cpp
#include "lib/aht20/HAHT20/HAHT20.hpp"

// The bus is created once by the application and lent out; the sensor never
// owns it and never closes it.
static HAHT20 thermometer(bus);

if (!thermometer.begin()) {
  HWarning("no AHT20 at 0x%02X", HAHT20_ADDRESS);
}

HAHT20Sample sample;
if (thermometer.measure(sample)) {
  HInfo("%.1f C, %.0f %%", sample.temperatureC, sample.humidity);
}
```

`measure()` blocks for 80 ms. See [Choosing a read style](#choosing-a-read-style).

## API

### `HAHT20Sample`

| Field | Meaning |
|---|---|
| `float temperatureC` | Degrees Celsius, −40 to +85 |
| `float humidity` | Relative humidity in percent, 0 to 100 |

Both values come out of one conversion. The sensor cannot be asked for
temperature alone, which is why a sample is one struct rather than two calls.

### `HAHT20`

| Method | Blocks | Notes |
|---|---|---|
| `HAHT20(bus, address, speedHz)` | — | Stores the bus. Transmits nothing; a constructor cannot report failure. |
| `bool begin()` | ~10 ms | Adds the device, checks it answers, calibrates if needed. |
| `bool isReady() const` | — | True once `begin()` has succeeded. |
| `bool measure(HAHT20Sample&)` | **80 ms** | Trigger, wait, read, check CRC. |
| `bool startMeasurement()` | — | Trigger only. |
| `bool isBusy()` | — | One status byte. Cheap enough to poll every scan. |
| `bool collect(HAHT20Sample&)` | — | Read the result of `startMeasurement()`. |
| `bool reset()` | ~35 ms | Soft reset, then re-run `begin()`. |

Every method returns `false` rather than throwing or aborting. The sample is
**written only on success** — a failed read cannot quietly zero a display.

## Choosing a read style

`measure()` blocks for 80 ms, which is sixteen engine scans. It belongs on the
main task or a task of its own, never inside a unit's `update()` or a
boundary's `read()`/`write()`.

For a scan-driven caller, split it across scans:

```cpp
sensor.startMeasurement();          // one scan

// a later scan, at least 80 ms after the trigger
if (!sensor.isBusy()) {
  sensor.collect(sample);
}
```

`isBusy()` returns **true when the sensor does not answer at all**. That is
deliberate: the alternative is telling the caller a result is waiting when the
sensor is not even there.

## Configuration

Override before including the header, or in the build.

| Macro | Default | Meaning |
|---|---|---|
| `HAHT20_ADDRESS` | `0x38` | Only useful behind a multiplexer. |
| `HAHT20_I2C_SPEED_HZ` | `400000` | The part's maximum. |
| `HAHT20_I2C_TIMEOUT_MS` | `100` | Per transfer. |
| `HAHT20_MEASURE_MS` | `80` | Conversion wait. 75 ms typical plus margin. |

## How often to read it

**Once every few seconds at most.** The die warms itself when read
continuously and the temperature drifts a few tenths of a degree high. The
application reads on the 5 s report tick, which is comfortably clear of it.

## Things that will catch you

**An AHT10 will not work.** It answers at the same address and takes the same
commands, but does not send the CRC byte this driver checks. Every read will
fail. That is intentional — the alternative is trusting unverified bytes.

**A cold sensor must be calibrated.** `begin()` reads the status register and
sends the load-coefficients command only when the bit is clear, so a sensor
that kept power across a reboot is not disturbed. Skipping this step entirely
gives readings that look plausible and are not.

**The bus is borrowed.** It must outlive the sensor object. The driver removes
its own device handle in the destructor and leaves the bus alone.

## Troubleshooting

| Symptom | Cause |
|---|---|
| `no sensor answering at 0x38` | Not powered, SDA/SCL swapped, or no pull-ups. |
| `sensor refused to calibrate` | Usually a supply below 2.2 V, or a clone. |
| `CRC mismatch` | An AHT10, marginal pull-ups, or a bus too long for 400 kHz. Try 100 kHz. |
| Humidity pinned at 100 % | Condensation on the die. It recovers once dry. |
| Temperature reads high | Self-heating from reading too often, or the sensor sitting near the regulator. |

## Protocol, for reference

| Step | Bytes |
|---|---|
| Read status | `0x71` → 1 byte. Bit 7 busy, bit 3 calibrated. |
| Calibrate | `0xBE 0x08 0x00`, then wait 10 ms |
| Trigger | `0xAC 0x33 0x00`, then wait 80 ms |
| Read result | 7 bytes: status, 20-bit humidity, 20-bit temperature, CRC |
| Soft reset | `0xBA`, then wait 20 ms |

Byte 3 is shared: its high nibble ends the humidity value, its low nibble
starts the temperature. The CRC is CRC-8 with polynomial `0x31` and seed
`0xFF` — the same one the Sensirion SHT3x family uses.

Scales: `RH% = raw × 100 / 2²⁰`, `°C = raw × 200 / 2²⁰ − 50`.
