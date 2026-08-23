#pragma once

#include <cstddef>

#include "HAdcChannel/HAdcChannel.hpp"

/**
 * @brief The board's analog pins, by name. A static facade.
 *
 * The ADC half of what HGpioManager is for digital pads, and deliberately the
 * same shape: the board table declares rows, configureAll() applies them once
 * at start-up, and everything above says find("vbat") rather than naming a
 * number.
 *
 * ## Why this is a module of its own and not a mode on HGpioManager
 * A pad and a converter are different peripherals with different vocabularies.
 * HIGpio speaks in levels - `readRaw() -> bool`, `writeRaw(bool)` - and that is
 * the whole of it; attenuation, bit width, calibration handles and millivolts
 * have nowhere to live behind that interface, and adding them would double what
 * every backend must implement including the desktop one, which has a trivial
 * fake for a level and no meaningful fake for a voltage curve.
 *
 * It would also have made HGpioManager a module you cannot describe without the
 * word "and": hands out pads AND runs conversions.
 *
 * ## The board table is still ONE table
 * The rows live in HGpioConfig.h beside the digital ones, in their own list, and
 * that is not a detail. A pad has one owner, and HGpioManager's
 * `allNumbersUnique()` static_assert spans BOTH lists - so declaring an analog
 * pin on a pad a button already uses fails the build. A second table in a second
 * file would have walked straight around the one check that catches it.
 *
 * @code
 *   HAdcManager::configureAll();
 *   const HAdcChannel battery = HAdcManager::find("vbat");
 *   const int packMv = battery.readMv();
 * @endcode
 */
class HAdcManager {
 public:
  HAdcManager() = delete;

  /**
   * @brief Applies every declared analog row. Call once, at start-up.
   *
   * A pin the platform rejects is logged and skipped rather than fatal: a
   * device with a broken analog input should still be a thermometer.
   *
   * @return false if any pin failed, having still configured the rest.
   */
  static bool configureAll() noexcept;

  /**
   * @brief The channel a board table row named.
   * @return An INVALID channel for an unknown name - it reads -1 from
   *         everything and is safe to use. A typo is a log line, not a crash.
   */
  static HAdcChannel find(const char* name) noexcept;

  /** @brief How many analog pins this board declares. */
  static size_t count() noexcept;

  /** @brief For iteration; an invalid channel past the end. */
  static HAdcChannel at(size_t index) noexcept;
};
