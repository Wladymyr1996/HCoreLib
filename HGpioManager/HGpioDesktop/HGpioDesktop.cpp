#include "HGpioDesktop.hpp"

HGpioDesktop::HGpioDesktop() noexcept : levels_() {
  levels_.fill(false);
}

HGpioDesktop::~HGpioDesktop() = default;

bool HGpioDesktop::inRange(int number) noexcept {
  return number >= 0 && static_cast<size_t>(number) < HGPIO_DESKTOP_PIN_COUNT;
}

bool HGpioDesktop::configure(const HGpioPinDesc& pin) noexcept {
  if (!inRange(pin.number)) {
    return false;
  }

  // An input starts at whatever its pull resistor would hold it at, so a test
  // that never touches the pad still reads what the board would read - an
  // idle pulled-up button reads HIGH here exactly as it does on the target.
  if (pin.dir == HGpioDir::Input) {
    levels_[static_cast<size_t>(pin.number)] = (pin.pull == HGpioPull::Up);
  } else {
    levels_[static_cast<size_t>(pin.number)] = false;  // Parked, as the target parks it.
  }

  return true;
}

bool HGpioDesktop::readRaw(int number) const noexcept {
  return inRange(number) ? levels_[static_cast<size_t>(number)] : false;
}

void HGpioDesktop::writeRaw(int number, bool level) noexcept {
  if (inRange(number)) {
    levels_[static_cast<size_t>(number)] = level;
  }
}

void HGpioDesktop::setRawLevel(int number, bool level) noexcept {
  writeRaw(number, level);
}
