#pragma once

#include <cstdint>

#include "../HIRgbLed/HIRgbLed.hpp"

/**
 * @brief HIRgbLed as three bytes in RAM. The backend of every host build.
 *
 * There is no LED on a PC, so this one remembers what it was last told and how
 * many times - which is what lets a test check HStatusLed's patterns, and that
 * it only writes the LED when the colour actually changes.
 *
 * A test reaches it through HStatusLed::driver():
 * @code
 *   auto& led = static_cast<HRgbLedDesktop&>(HStatusLed::driver());
 *   CHECK(led.green() > 0);
 * @endcode
 */
class HRgbLedDesktop : public HIRgbLed {
 public:
  bool begin() noexcept override;
  void show(uint8_t red, uint8_t green, uint8_t blue) noexcept override;

  uint8_t red() const noexcept { return red_; }
  uint8_t green() const noexcept { return green_; }
  uint8_t blue() const noexcept { return blue_; }

  /** @brief True when the last colour shown was not black. */
  bool isLit() const noexcept { return red_ != 0 || green_ != 0 || blue_ != 0; }

  /** @brief How many times show() has been called. */
  uint32_t writes() const noexcept { return writes_; }

 private:
  uint8_t red_ = 0;
  uint8_t green_ = 0;
  uint8_t blue_ = 0;
  uint32_t writes_ = 0;
};
