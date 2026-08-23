#include "HAdcDesktop.hpp"

#define HLOG_MODULE_NAME "HAdc"
#include <HLog/HLog.hpp>

namespace {

/**
 * Counts a pretend 12-bit converter reports, so readRaw() is not simply the
 * millivolts again. The host has no reference voltage, so this is the range the
 * target's 12 dB attenuation covers - close enough that a test exercising the
 * raw path sees numbers of the right magnitude.
 */
constexpr int kFullScaleMv = 3100;
constexpr int kFullScaleCounts = 4095;

}  // namespace

HAdcDesktop::HAdcDesktop() noexcept : configured_{}, millivolts_{}, failing_(false) {}

bool HAdcDesktop::isUsable(int number) const noexcept {
  return number >= 0 && number < HADC_DESKTOP_PIN_COUNT && configured_[number] && !failing_;
}

bool HAdcDesktop::configure(const HAdcPinDesc& pin) noexcept {
  if (pin.number < 0 || pin.number >= HADC_DESKTOP_PIN_COUNT) {
    HWarning("'%s' is pin %d, which this host backend does not have", pin.name, pin.number);
    return false;
  }

  configured_[pin.number] = true;
  return true;
}

int HAdcDesktop::readRaw(int number) const noexcept {
  if (!isUsable(number)) {
    return -1;
  }

  const int counts = (millivolts_[number] * kFullScaleCounts) / kFullScaleMv;
  return (counts > kFullScaleCounts) ? kFullScaleCounts : counts;
}

int HAdcDesktop::readMv(int number) const noexcept {
  if (!isUsable(number)) {
    return -1;
  }

  return millivolts_[number];
}

void HAdcDesktop::setMv(int number, int millivolts) noexcept {
  if (number < 0 || number >= HADC_DESKTOP_PIN_COUNT) {
    return;
  }

  millivolts_[number] = millivolts;
}

void HAdcDesktop::setFailing(bool failing) noexcept {
  failing_ = failing;
}

void HAdcDesktop::reset() noexcept {
  for (int i = 0; i < HADC_DESKTOP_PIN_COUNT; ++i) {
    configured_[i] = false;
    millivolts_[i] = 0;
  }
  failing_ = false;
}
