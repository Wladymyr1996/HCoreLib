#pragma once

#include <cstddef>

#include "../HAdcTypes/HAdcTypes.hpp"

/**
 * How many conversions one reading is made of.
 *
 * The ESP32's converter is genuinely noisy - single conversions wander by tens
 * of counts - and every caller wants that smoothed while none wants to write
 * the smoothing. Five is enough for a MEDIAN to reject a spike outright, and
 * odd so the middle is a real sample rather than an average of two.
 *
 * A median rather than a mean on purpose: one absurd conversion moves a mean
 * and cannot move a median at all, and absurd conversions are exactly what a
 * high-impedance divider produces.
 */
#ifndef HADC_SAMPLES
#define HADC_SAMPLES 5
#endif

/**
 * @brief One analog input, by name. A cheap value type, like HGpioPin.
 *
 * Holds a pointer to its compile-time row and nothing else - no calibration, no
 * buffers - so it is free to copy and may be stored anywhere. Everything it
 * needs to reach the converter it gets from hAdcBackend().
 *
 * @code
 *   const HAdcChannel battery = HAdcManager::find("vbat");
 *   const int millivolts = battery.readMv();   // at the BATTERY, divider applied
 *   if (millivolts < 0) {
 *     // not measured - never a plausible zero
 *   }
 * @endcode
 *
 * ## Two millivolt readings, and the difference matters
 * readPadMv() is what the chip saw. readMv() is that multiplied by the divider
 * declared in the board table - the voltage at the SOURCE. Application code
 * wants the second and should never see the first; readPadMv() exists for
 * bring-up, when the question is whether the divider is the value somebody
 * thinks it is.
 *
 * An invalid channel - one HAdcManager::find() could not name - reads -1 from
 * everything and is safe to use. A name that does not exist is a mistake worth
 * a log line, not a crash.
 */
class HAdcChannel {
 public:
  /** @brief An invalid channel. Reads -1 from everything. */
  HAdcChannel() noexcept;

  /** @param desc A row from the board table, which outlives everything. */
  explicit HAdcChannel(const HAdcPinDesc* desc) noexcept;

  bool isValid() const noexcept;

  /** @brief The name the board table gave it, or "" when invalid. Never null. */
  const char* name() const noexcept;

  /** @brief The chip pin, or -1 when invalid. */
  int number() const noexcept;

  /** @brief What this pad divides by. 1.0 when there is no divider. */
  float divider() const noexcept;

  /**
   * @brief The median of HADC_SAMPLES raw conversions.
   * @return -1 when the channel is invalid or the converter failed.
   */
  int readRaw() const noexcept;

  /**
   * @brief The median of HADC_SAMPLES readings, in millivolts AT THE PAD.
   *
   * For bring-up. Application code wants readMv().
   * @return -1 when the channel is invalid, uncalibrated, or the read failed.
   */
  int readPadMv() const noexcept;

  /**
   * @brief The voltage at the SOURCE, in millivolts - the divider undone.
   *
   * What a battery gauge, a thermistor or anything else behind a divider
   * actually wants. Nothing above this ever learns there were resistors.
   *
   * @return -1 when the channel is invalid or the reading failed. Never a
   *         plausible zero for something that was not measured.
   */
  int readMv() const noexcept;

 private:
  /** @brief Median of HADC_SAMPLES calls to whichever read this is. */
  int median(bool millivolts) const noexcept;

  const HAdcPinDesc* desc_;
};
