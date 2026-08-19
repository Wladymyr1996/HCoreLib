#pragma once

#include "../HGpioTypes/HGpioTypes.hpp"

/**
 * @brief The pad-level contract: configure a pin, read it, drive it.
 *
 * Deliberately the smallest thing that can be implemented twice. It speaks in
 * PHYSICAL levels and pin numbers - inversion, names and the board table all
 * live above it, in HGpioPin and HGpioManager - so a backend is a few lines of
 * platform call and has no policy to get wrong.
 *
 * Exactly one implementation is compiled into a given build, selected by
 * IS_MCU. Do not name a backend in application code: go through
 * HGpioManager::find() and the HGpioPin it hands back.
 */
class HIGpio {
 public:
  virtual ~HIGpio();

  /**
   * @brief Applies direction and pull to one pad, and parks an output LOW.
   * @param pin The declaration to apply.
   * @return false if the platform rejected the pin.
   */
  virtual bool configure(const HGpioPinDesc& pin) noexcept = 0;

  /** @brief The pad's PHYSICAL level. No inversion - that is HGpioPin's job. */
  virtual bool readRaw(int number) const noexcept = 0;

  /** @brief Drives the pad to a PHYSICAL level. No inversion. */
  virtual void writeRaw(int number, bool level) noexcept = 0;
};
