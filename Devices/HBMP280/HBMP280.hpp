#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/i2c_master.h>

/**
 * Default 7-bit address, the one with SDO strapped high - which is what the
 * module on this device does. A board that ties SDO low answers at 0x76
 * instead; the small purple GY-BMP280 breakouts usually do.
 *
 * Getting this wrong is not silent: begin() finds nothing and says to try the
 * other one.
 */
#ifndef HBMP280_ADDRESS
#define HBMP280_ADDRESS 0x77
#endif

/** I2C clock. The part is rated to 3.4 MHz; 400 kHz matches everything else here. */
#ifndef HBMP280_I2C_SPEED_HZ
#define HBMP280_I2C_SPEED_HZ 400000
#endif

/** How long any single I2C transfer may take before it is called a failure. */
#ifndef HBMP280_I2C_TIMEOUT_MS
#define HBMP280_I2C_TIMEOUT_MS 100
#endif

/** Sea-level pressure in pascals, standard atmosphere. The altitude reference. */
#ifndef HBMP280_SEA_LEVEL_PA
#define HBMP280_SEA_LEVEL_PA 101325.0f
#endif

/**
 * @brief How many times a channel is sampled and averaged inside the sensor.
 *
 * More samples cost time and current and buy resolution: x1 gives pressure to
 * about 2.6 Pa of noise, x16 to about 0.9 Pa. See HBMP280::measureTimeMs() for
 * what each one costs.
 *
 * The datasheet also allows a channel to be skipped entirely. This driver does
 * not offer that: pressure compensation needs the temperature reading as an
 * input, so a skipped channel produces a wrong number rather than no number.
 */
enum class HBMP280Oversampling : uint8_t {
  X1 = 1,
  X2 = 2,
  X4 = 3,
  X8 = 4,
  X16 = 5,
};

/**
 * @brief The sensor's internal IIR filter, applied to pressure and temperature.
 *
 * It suppresses short spikes - a door slamming shifts indoor pressure enough to
 * show up plainly in raw readings - at the cost of settling over several
 * samples. Off is right for a weather log where every sample stands alone.
 */
enum class HBMP280Filter : uint8_t {
  Off = 0,
  X2 = 1,
  X4 = 2,
  X8 = 3,
  X16 = 4,
};

/** @brief One conversion. Both values come from the same read, and must. */
struct HBMP280Sample {
  /** Degrees Celsius. Range -40 to +85, accurate to about +-1 C. */
  float temperatureC;

  /** Pressure in pascals. Range 30000 to 110000, accurate to about +-100 Pa. */
  float pressurePa;
};

/**
 * @brief A BMP280 pressure and temperature sensor on a borrowed I2C bus.
 *
 * The bus is never owned - it is created once in the application and shared,
 * which is how the panel and the AHT20 already work.
 *
 * @code
 *   HBMP280 barometer(bus);
 *   barometer.begin();
 *
 *   HBMP280Sample sample;
 *   if (barometer.measure(sample)) {
 *     HInfo("%.1f C, %.0f Pa", sample.temperatureC, sample.pressurePa);
 *   }
 * @endcode
 *
 * ## Forced mode, not normal mode
 * The sensor is left asleep and woken for one conversion at a time. Normal
 * mode - where it free-runs on its own timer - is the right answer only for a
 * caller that reads faster than the standby period, and nothing here does. In
 * forced mode the part draws well under a microamp between readings.
 *
 * ## Not for the scan
 * measure() blocks for the conversion: about 8 ms at the defaults, up to 45 ms
 * at x16/x16. Same rule as the display and the AHT20 - its own task or startup.
 * A scan-driven caller uses the three-part form, which never blocks:
 *
 * @code
 *   barometer.startMeasurement();
 *   ...
 *   if (!barometer.isBusy()) {    // measureTimeMs() later
 *     barometer.collect(sample);
 *   }
 * @endcode
 *
 * ## Its temperature is not the room's
 * The temperature channel exists because pressure compensation needs it. The
 * die sits next to its own regulator and reads a little high - fine as a
 * compensation input, worse than the AHT20 as a thermometer. Prefer the AHT20
 * for anything shown to a person.
 *
 * ## BMP280, not BME280
 * begin() checks the chip ID and refuses anything else. A BME280 is pin- and
 * register-compatible up to a point and answers at the same addresses, but it
 * has a humidity channel that must be configured before it will measure at
 * all - so it would appear to work here and quietly return stale pressure.
 *
 * Not thread-safe: one task owns one sensor.
 */
class HBMP280 {
 public:
  /**
   * @param bus     An open bus from i2c_new_master_bus(). Borrowed - it must
   *                outlive this object. Stored only; nothing is transmitted
   *                until begin(), because a constructor cannot report failure.
   * @param address 7-bit I2C address. 0x77 or 0x76, decided by the SDO pin.
   * @param speedHz Clock for this device; others on the bus keep their own.
   */
  explicit HBMP280(i2c_master_bus_handle_t bus, uint8_t address = HBMP280_ADDRESS,
                   uint32_t speedHz = HBMP280_I2C_SPEED_HZ);
  ~HBMP280();

  HBMP280(const HBMP280&) = delete;
  HBMP280& operator=(const HBMP280&) = delete;

  /**
   * @brief Identifies the sensor, resets it, reads its calibration and configures it.
   *
   * The calibration is twenty-four bytes of per-part coefficients burned in at
   * the factory. They are read once here and kept, because every reading is
   * meaningless without them - a raw BMP280 value is not a pressure in any
   * unit until it has been through them.
   *
   * The defaults suit a room barometer: pressure oversampled hard because it is
   * the reading that matters, temperature lightly because it is only an input
   * to that, and a mild filter to keep doors and draughts out of the trend.
   *
   * @return false if nothing answered, if the chip ID is not a BMP280, or if
   *         the calibration looks unset. Every later call then fails cleanly.
   */
  bool begin(HBMP280Oversampling temperature = HBMP280Oversampling::X2,
             HBMP280Oversampling pressure = HBMP280Oversampling::X16,
             HBMP280Filter filter = HBMP280Filter::X4);

  /** @brief True once the sensor has been identified and configured. */
  bool isReady() const;

  /**
   * @brief Wakes the sensor for one conversion, waits for it, and returns the result.
   *
   * Blocks for measureTimeMs(). See the class note - never from the scan.
   *
   * @param sample Written only when the call returns true; untouched otherwise,
   *               so a failed read cannot quietly zero a display.
   */
  bool measure(HBMP280Sample& sample);

  /** @brief Starts one conversion and returns immediately. Pair with isBusy() and collect(). */
  bool startMeasurement();

  /**
   * @brief True while a conversion is still running.
   *
   * A sensor that has stopped answering reads as busy, which keeps collect()
   * from being called on a dead bus.
   */
  bool isBusy();

  /**
   * @brief Reads and compensates the result of the conversion started above.
   *
   * @param sample Written only when the call returns true.
   * @return false if the read failed, or if the calibration cannot produce a
   *         pressure from these values.
   */
  bool collect(HBMP280Sample& sample);

  /**
   * @brief How long one conversion takes at the current settings, in milliseconds.
   *
   * Computed from the datasheet's timing formula and rounded up, rather than
   * fixed, so changing the oversampling cannot leave measure() reading a
   * result that is not there yet.
   */
  uint32_t measureTimeMs() const;

  /** @brief Soft-resets the sensor and configures it again. Blocks for about 10 ms. */
  bool reset();

  /**
   * @brief Altitude in metres for a pressure, against a sea-level reference.
   *
   * The standard barometric formula. Its accuracy is entirely the reference's:
   * with the default 101325 Pa this is only as good as today's weather, and can
   * be tens of metres out. Pass the local sea-level-adjusted pressure from a
   * nearby station to get something trustworthy.
   *
   * Useful as a relative measure regardless - the error is nearly constant over
   * minutes, so differences are far better than absolutes.
   */
  static float altitudeM(float pressurePa, float seaLevelPa = HBMP280_SEA_LEVEL_PA);

 private:
  /** @brief The factory coefficients, exactly as they are laid out in the sensor. */
  struct Calibration {
    uint16_t t1;
    int16_t t2;
    int16_t t3;
    uint16_t p1;
    int16_t p2;
    int16_t p3;
    int16_t p4;
    int16_t p5;
    int16_t p6;
    int16_t p7;
    int16_t p8;
    int16_t p9;
  };

  bool readRegisters(uint8_t reg, uint8_t* buffer, size_t length);
  bool writeRegister(uint8_t reg, uint8_t value);
  bool readCalibration();
  bool configure();

  /**
   * @brief The datasheet's integer temperature compensation.
   *
   * Also produces fine_, which the pressure compensation needs - which is why
   * temperature must always be compensated first, and why the two cannot be
   * separated into independent calls.
   *
   * @return Temperature in hundredths of a degree Celsius.
   */
  int32_t compensateTemperature(int32_t raw);

  /**
   * @brief The datasheet's 64-bit integer pressure compensation.
   *
   * @param pressure Written in Q24.8 pascals - 256ths of a pascal.
   * @return false if the coefficients would divide by zero, which means the
   *         calibration was never read or came back corrupt.
   */
  bool compensatePressure(int32_t raw, uint32_t& pressure) const;

  i2c_master_bus_handle_t bus_;
  i2c_master_dev_handle_t device_;
  uint8_t address_;
  uint32_t speedHz_;
  bool ready_;

  HBMP280Oversampling temperatureOversampling_;
  HBMP280Oversampling pressureOversampling_;
  HBMP280Filter filter_;

  Calibration calibration_;

  /** Carries temperature into the pressure compensation. Set by every conversion. */
  int32_t fine_;
};
