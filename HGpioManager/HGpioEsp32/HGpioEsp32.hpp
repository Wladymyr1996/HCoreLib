#pragma once

#include "../HIGpio/HIGpio.hpp"

/**
 * @brief HIGpio on the ESP-IDF driver. The backend of every target build.
 *
 * Stateless: one static instance lives in HGpioManager.cpp and every call goes
 * straight through to gpio_config()/gpio_get_level()/gpio_set_level(). Nothing
 * here is ever heap-allocated, and nothing here knows a pin's name or its
 * inversion.
 */
class HGpioEsp32 : public HIGpio {
 public:
  ~HGpioEsp32() override;

  /** @brief gpio_config() for one pad, then parks an output LOW. */
  bool configure(const HGpioPinDesc& pin) noexcept override;

  /** @brief gpio_get_level(). */
  bool readRaw(int number) const noexcept override;

  /** @brief gpio_set_level(). */
  void writeRaw(int number, bool level) noexcept override;
};
