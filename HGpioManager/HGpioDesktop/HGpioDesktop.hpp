#pragma once

#include <cstddef>

#include <etl/array.h>

#include "../HIGpio/HIGpio.hpp"

/** @brief Pads the desktop backend pretends to have. Wider than any real chip. */
#ifndef HGPIO_DESKTOP_PIN_COUNT
#define HGPIO_DESKTOP_PIN_COUNT 48
#endif

/**
 * @brief HIGpio as an array in RAM. The backend of every host build.
 *
 * There are no pads on a PC, so this one simply remembers a level per pin
 * number. That is not a stub for its own sake: it is what lets the logic built
 * on top - HButton's debounce and long-press timing above all - be driven
 * through a synthetic bounce train in a test, on a machine with no hardware
 * attached, and be the very same code that runs on the target.
 *
 * A test reaches it through HGpioManager::instance():
 *
 * @code
 *   auto& gpio = static_cast<HGpioDesktop&>(HGpioManager::instance());
 *   gpio.setRawLevel(2, false);   // button pulled to ground
 * @endcode
 */
class HGpioDesktop : public HIGpio {
 public:
  HGpioDesktop() noexcept;
  ~HGpioDesktop() override;

  /** @brief Records the direction and seeds the pad from its pull resistor. */
  bool configure(const HGpioPinDesc& pin) noexcept override;

  /** @brief The remembered PHYSICAL level. */
  bool readRaw(int number) const noexcept override;

  /** @brief Remembers a PHYSICAL level as if the pad had been driven. */
  void writeRaw(int number, bool level) noexcept override;

  /**
   * @brief Test hook: forces a pad's physical level, as a wire would.
   * @param number Pin number; out-of-range numbers are ignored.
   * @param level The physical level a reader should now see.
   */
  void setRawLevel(int number, bool level) noexcept;

 private:
  /** @brief True while `number` addresses a pad this backend pretends to have. */
  static bool inRange(int number) noexcept;

  etl::array<bool, HGPIO_DESKTOP_PIN_COUNT> levels_;
};
