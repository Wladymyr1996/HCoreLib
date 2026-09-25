#include "HRgbLedDesktop.hpp"

bool HRgbLedDesktop::begin() noexcept {
  return true;
}

void HRgbLedDesktop::show(uint8_t red, uint8_t green, uint8_t blue) noexcept {
  red_ = red;
  green_ = green;
  blue_ = blue;
  ++writes_;
}
