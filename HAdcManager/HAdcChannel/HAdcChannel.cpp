#include "HAdcChannel.hpp"

#include "../HAdcBackend/HAdcBackend.hpp"
#include "../HIAdc/HIAdc.hpp"

namespace {

/** @brief The empty row an invalid channel points at, so nothing dereferences null. */
constexpr HAdcPinDesc kInvalid = {};

}  // namespace

HAdcChannel::HAdcChannel() noexcept : desc_(nullptr) {}

HAdcChannel::HAdcChannel(const HAdcPinDesc* desc) noexcept : desc_(desc) {}

bool HAdcChannel::isValid() const noexcept {
  return desc_ != nullptr && desc_->number >= 0;
}

const char* HAdcChannel::name() const noexcept {
  return (desc_ != nullptr) ? desc_->name : kInvalid.name;
}

int HAdcChannel::number() const noexcept {
  return (desc_ != nullptr) ? desc_->number : -1;
}

float HAdcChannel::divider() const noexcept {
  return (desc_ != nullptr) ? desc_->divider : 1.0f;
}

int HAdcChannel::median(bool millivolts) const noexcept {
  if (!isValid()) {
    return -1;
  }

  HIAdc& backend = hAdcBackend();

  int samples[HADC_SAMPLES] = {};
  size_t taken = 0;

  for (size_t i = 0; i < HADC_SAMPLES; ++i) {
    const int value =
        millivolts ? backend.readMv(desc_->number) : backend.readRaw(desc_->number);

    // A failed conversion is DROPPED rather than sorted. Sorting -1 in would
    // drag the median down and report a low battery for a bad read, which is
    // the one wrong answer that matters here.
    if (value < 0) {
      continue;
    }

    // Insertion sort, kept ordered as it goes: HADC_SAMPLES is single digits,
    // so this is fewer comparisons than any cleverer sort and needs no scratch.
    size_t at = taken;
    while (at > 0 && samples[at - 1] > value) {
      samples[at] = samples[at - 1];
      --at;
    }
    samples[at] = value;
    ++taken;
  }

  if (taken == 0) {
    return -1;
  }

  return samples[taken / 2];
}

int HAdcChannel::readRaw() const noexcept {
  return median(false);
}

int HAdcChannel::readPadMv() const noexcept {
  return median(true);
}

int HAdcChannel::readMv() const noexcept {
  const int atPad = readPadMv();
  if (atPad < 0) {
    return -1;
  }

  const float ratio = divider();
  if (ratio <= 0.0f) {
    // A divider of zero or less is a table somebody mistyped. Refusing is the
    // only honest answer: multiplying by it would report 0 mV, which reads as
    // a flat battery rather than as a broken configuration.
    return -1;
  }

  return static_cast<int>(static_cast<float>(atPad) * ratio + 0.5f);
}
