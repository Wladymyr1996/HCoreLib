#include "HGpioEsp32.hpp"

#define HLOG_MODULE_NAME "HGpio"
#include <HLog/HLog.hpp>

#include <driver/gpio.h>

HGpioEsp32::~HGpioEsp32() = default;

bool HGpioEsp32::configure(const HGpioPinDesc& pin) noexcept {
  const gpio_num_t number = static_cast<gpio_num_t>(pin.number);

  gpio_config_t config = {};
  config.pin_bit_mask = 1ULL << pin.number;
  config.mode = (pin.dir == HGpioDir::Output) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;

  // Pulls are applied to inputs only. An output pad drives both rails, and
  // leaving a pull enabled on it just burns current through the driver - which
  // matters on a battery.
  const bool isInput = (pin.dir == HGpioDir::Input);
  config.pull_up_en = (isInput && pin.pull == HGpioPull::Up) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  config.pull_down_en = (isInput && pin.pull == HGpioPull::Down) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;

  const esp_err_t result = gpio_config(&config);
  if (result != ESP_OK) {
    HCritical("gpio_config(%s -> GPIO%d) failed: %s", pin.name, pin.number, esp_err_to_name(result));
    return false;
  }

  if (pin.dir == HGpioDir::Output) {
    // Parked at the PHYSICAL low, not the logical false: this runs before
    // anything has a value to drive, and the point is to leave the pad in a
    // known state rather than to assert a meaning.
    gpio_set_level(number, 0);
  }

  return true;
}

bool HGpioEsp32::readRaw(int number) const noexcept {
  return gpio_get_level(static_cast<gpio_num_t>(number)) != 0;
}

void HGpioEsp32::writeRaw(int number, bool level) noexcept {
  gpio_set_level(static_cast<gpio_num_t>(number), level ? 1 : 0);
}
