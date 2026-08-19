#include "HGpioPin.hpp"

#include "../HGpioBackend/HGpioBackend.hpp"
#include "../HIGpio/HIGpio.hpp"

namespace {

/**
 * @brief Applies inversion. Logical -> physical and physical -> logical are the
 *        same operation, which is why one function serves both directions.
 */
bool applyInvert(bool value, bool invert) noexcept {
  return invert ? !value : value;
}

}  // namespace

HGpioPin::HGpioPin() noexcept : desc_(nullptr) {
}

HGpioPin::HGpioPin(const HGpioPinDesc* desc) noexcept : desc_(desc) {
}

bool HGpioPin::isValid() const noexcept {
  return desc_ != nullptr;
}

bool HGpioPin::read() const noexcept {
  if (desc_ == nullptr) {
    return false;
  }
  return applyInvert(hGpioBackend().readRaw(desc_->number), desc_->invert);
}

void HGpioPin::write(bool value) const noexcept {
  if (desc_ == nullptr || desc_->dir != HGpioDir::Output) {
    return;
  }
  hGpioBackend().writeRaw(desc_->number, applyInvert(value, desc_->invert));
}

const char* HGpioPin::name() const noexcept {
  return (desc_ != nullptr) ? desc_->name : "";
}

int HGpioPin::number() const noexcept {
  return (desc_ != nullptr) ? desc_->number : -1;
}

bool HGpioPin::isOutput() const noexcept {
  return (desc_ != nullptr) && desc_->dir == HGpioDir::Output;
}

bool HGpioPin::isActiveLow() const noexcept {
  return (desc_ != nullptr) && desc_->invert;
}
