#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/i2c_master.h>

/**
 * The address is fixed in silicon: an AHT20 has no address pins and no strap,
 * so exactly one of them can live on a bus. It is a macro only so a board with
 * an I2C multiplexer can override it.
 */
#ifndef HAHT20_ADDRESS
#define HAHT20_ADDRESS 0x38
#endif

/** I2C clock. The part is rated to 400 kHz, the same fast mode as the panel. */
#ifndef HAHT20_I2C_SPEED_HZ
#define HAHT20_I2C_SPEED_HZ 400000
#endif

/** How long any single I2C transfer may take before it is called a failure. */
#ifndef HAHT20_I2C_TIMEOUT_MS
#define HAHT20_I2C_TIMEOUT_MS 100
#endif

/**
 * How long a conversion takes. The datasheet says 75 ms typical and asks for
 * 80 ms before the result is read; the extra 5 ms is the margin, not a guess.
 */
#ifndef HAHT20_MEASURE_MS
#define HAHT20_MEASURE_MS 80
#endif

/**
 * @brief One conversion: both values come out of the same 6 bytes, always
 *        together.
 *
 * There is no way to ask an AHT20 for temperature alone - humidity is measured
 * in the same cycle - so a sample is one struct rather than two calls.
 */
struct HAHT20Sample {
  /** Degrees Celsius. Range -40 to +85, accurate to about +-0.3 C. */
  float temperatureC;

  /** Relative humidity in percent, 0 to 100, accurate to about +-2 %. */
  float humidity;
};

/**
 * @brief An AHT20 temperature and humidity sensor on a borrowed I2C bus.
 *
 * The bus is never owned - it is created once in the application and shared
 * with the display, which is the whole reason HSSD1306 takes a bus handle
 * rather than a pair of pins.
 *
 * @code
 *   HAHT20 sensor(bus);
 *   sensor.begin();
 *
 *   HAHT20Sample sample;
 *   if (sensor.measure(sample)) {
 *     HInfo("%.1f C, %.0f %%", sample.temperatureC, sample.humidity);
 *   }
 * @endcode
 *
 * ## Not for the scan
 * measure() **sleeps for 80 ms** while the sensor converts - sixteen engine
 * scans. It belongs on its own task or in startup. A scan-driven caller uses
 * the three-part form instead, which never blocks:
 *
 * @code
 *   sensor.startMeasurement();      // one scan
 *   ...
 *   if (!sensor.isBusy()) {         // a later scan, >= 80 ms after the trigger
 *     sensor.collect(sample);
 *   }
 * @endcode
 *
 * ## Self-heating
 * The die warms itself if it is read continuously. Once every few seconds is
 * the sensible ceiling for a room thermometer; polling it flat out reads high
 * by a few tenths of a degree.
 *
 * ## AHT20, not AHT10
 * Every reply is checked against the CRC byte the AHT20 appends. An AHT10 does
 * not send that byte, so it will not work with this driver even though it
 * answers at the same address and speaks the same commands.
 *
 * Not thread-safe: one task owns one sensor.
 */
class HAHT20 {
 public:
  /**
   * @param bus     An open bus from i2c_new_master_bus(). Borrowed - it must
   *                outlive this object. Stored only; nothing is transmitted
   *                until begin(), because a constructor cannot report failure.
   * @param address 7-bit I2C address. See HAHT20_ADDRESS - there is only one.
   * @param speedHz Clock for this device; others on the bus keep their own.
   */
  explicit HAHT20(i2c_master_bus_handle_t bus, uint8_t address = HAHT20_ADDRESS,
                  uint32_t speedHz = HAHT20_I2C_SPEED_HZ);
  ~HAHT20();

  HAHT20(const HAHT20&) = delete;
  HAHT20& operator=(const HAHT20&) = delete;

  /**
   * @brief Adds the sensor to the bus and calibrates it if it is not already.
   *
   * A cold AHT20 comes up uncalibrated and reports nonsense until it is told
   * to load its factory coefficients, so this checks the status byte and sends
   * the load command only when the bit is clear - a sensor that survived a
   * reboot with its power on is not disturbed.
   *
   * @return false if nothing answered at the address, or if the sensor refused
   *         to calibrate. Every later call then fails cleanly rather than
   *         returning invented numbers.
   */
  bool begin();

  /** @brief True once the sensor has answered and reported itself calibrated. */
  bool isReady() const;

  /**
   * @brief Triggers a conversion, waits for it, and returns the result.
   *
   * Blocks for HAHT20_MEASURE_MS. See the class note - never call this from
   * the engine's scan.
   *
   * @param sample Written only when the call returns true; untouched otherwise,
   *               so a failed read cannot quietly zero a display.
   * @return false if the sensor is not ready, did not answer, was still busy
   *         after the wait, or sent a reply that failed its CRC.
   */
  bool measure(HAHT20Sample& sample);

  /**
   * @brief Starts a conversion and returns immediately.
   *
   * The result is not readable for HAHT20_MEASURE_MS. Pair with isBusy() and
   * collect().
   */
  bool startMeasurement();

  /**
   * @brief True while a conversion is still running.
   *
   * One byte off the wire, so it is cheap enough to ask every scan. A sensor
   * that has stopped answering reads as busy - which keeps collect() from
   * being called on a dead bus.
   */
  bool isBusy();

  /**
   * @brief Reads the result of the conversion started by startMeasurement().
   *
   * @param sample Written only when the call returns true.
   * @return false if the conversion is not finished yet, or if the reply
   *         failed its CRC.
   */
  bool collect(HAHT20Sample& sample);

  /**
   * @brief Soft-resets the sensor and calibrates it again.
   *
   * The way out of a sensor that has wedged - a wire glitched mid-transfer,
   * say. Blocks for about 30 ms.
   */
  bool reset();

 private:
  /** @brief Reads the one-byte status register. False if the sensor did not answer. */
  bool readStatus(uint8_t& status);

  /** @brief Sends the "load factory calibration" command and waits for it. */
  bool calibrate();

  /** @brief CRC-8, polynomial 0x31, seed 0xFF - what the AHT20 appends to a reply. */
  static uint8_t crc8(const uint8_t* data, size_t length);

  i2c_master_bus_handle_t bus_;
  i2c_master_dev_handle_t device_;
  uint8_t address_;
  uint32_t speedHz_;
  bool ready_;
};
