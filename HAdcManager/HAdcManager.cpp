#include "HAdcManager.hpp"

#define HLOG_MODULE_NAME "HAdc"
#include <HLog/HLog.hpp>

// The board's analog table. Owned by the application, never by the library -
// the same header the digital pins are declared in, because a pad has one owner
// and one file has to be able to see every claim on it.
#include <HGpioConfig.h>

// A board that declares no analog pins - which is most of them - must still
// build this module. An empty list costs it nothing but the facade.
#ifndef HGPIO_ANALOG_PINS
#define HGPIO_ANALOG_PINS(PIN)
#endif

#include "HAdcBackend/HAdcBackend.hpp"
#include "HIAdc/HIAdc.hpp"

#if IS_MCU
#include <soc/adc_channel.h>
#endif

namespace {

/** @brief Counts a row without looking at it, so an EMPTY list totals zero. */
#define HADC_COUNT_PIN(...) +1

/** @brief Expands one analog row into a table entry. */
#define HADC_MAKE_PIN(name_, number_, atten_, divider_) \
  HAdcPinDesc{name_, number_, HAdcAtten::atten_, divider_},

constexpr size_t kPinCount = 0 HGPIO_ANALOG_PINS(HADC_COUNT_PIN);

/**
 * @brief Storage for the table, never zero-length.
 *
 * A board with no analog pins is legal and costs nothing, but a zero-length
 * array is not - so the table keeps one unused row in that case. kPinCount, not
 * this, is what every loop runs to.
 */
constexpr size_t kTableStorage = (kPinCount > 0) ? kPinCount : 1;

constexpr HAdcPinDesc kPins[kTableStorage] = {HGPIO_ANALOG_PINS(HADC_MAKE_PIN)};

#undef HADC_COUNT_PIN
#undef HADC_MAKE_PIN

// ---------------------------------------------------------------------------
// Compile-time validation of the analog table.
//
// The duplicate-pad check is NOT here - it lives in HGpioManager.cpp, which is
// the one place that can see the digital rows and these rows at once. A pad has
// one owner, and only a check that spans both lists can say so.
//
// What is here is the question only this module can answer: whether the chip
// can convert on that pad at all.
// ---------------------------------------------------------------------------

#if IS_MCU

/**
 * @brief True when `number` is an ADC1 pad on this chip.
 *
 * Built from the chip's own soc/adc_channel.h rather than a table written here,
 * so a different target is a recompile rather than an edit somebody has to
 * remember. On the ESP32-C6 that is GPIO0-6; the channels this chip does not
 * have are simply not defined.
 */
constexpr bool isAdc1Pad(int number) {
  return false
#ifdef ADC1_CHANNEL_0_GPIO_NUM
         || number == ADC1_CHANNEL_0_GPIO_NUM
#endif
#ifdef ADC1_CHANNEL_1_GPIO_NUM
         || number == ADC1_CHANNEL_1_GPIO_NUM
#endif
#ifdef ADC1_CHANNEL_2_GPIO_NUM
         || number == ADC1_CHANNEL_2_GPIO_NUM
#endif
#ifdef ADC1_CHANNEL_3_GPIO_NUM
         || number == ADC1_CHANNEL_3_GPIO_NUM
#endif
#ifdef ADC1_CHANNEL_4_GPIO_NUM
         || number == ADC1_CHANNEL_4_GPIO_NUM
#endif
#ifdef ADC1_CHANNEL_5_GPIO_NUM
         || number == ADC1_CHANNEL_5_GPIO_NUM
#endif
#ifdef ADC1_CHANNEL_6_GPIO_NUM
         || number == ADC1_CHANNEL_6_GPIO_NUM
#endif
#ifdef ADC1_CHANNEL_7_GPIO_NUM
         || number == ADC1_CHANNEL_7_GPIO_NUM
#endif
#ifdef ADC1_CHANNEL_8_GPIO_NUM
         || number == ADC1_CHANNEL_8_GPIO_NUM
#endif
#ifdef ADC1_CHANNEL_9_GPIO_NUM
         || number == ADC1_CHANNEL_9_GPIO_NUM
#endif
      ;
}

/** @brief True if every declared analog pin sits on a pad that can convert. */
constexpr bool allAnalogPinsHaveAdc() {
  for (size_t i = 0; i < kPinCount; ++i) {
    if (!isAdc1Pad(kPins[i].number)) {
      return false;
    }
  }
  return true;
}

static_assert(allAnalogPinsHaveAdc(),
              "HGPIO_ANALOG_PINS declares a pad this chip cannot convert on - "
              "ADC1 only, and ADC2 is not usable while the radio is up");

#endif  // IS_MCU

/** @brief True if no divider is zero or negative, which would report a flat source. */
constexpr bool allDividersUsable() {
  for (size_t i = 0; i < kPinCount; ++i) {
    if (!(kPins[i].divider > 0.0f)) {
      return false;
    }
  }
  return true;
}

static_assert(allDividersUsable(),
              "HGPIO_ANALOG_PINS declares a divider of zero or less - a divider is what "
              "the reading is MULTIPLIED by, so 1.0 means 'wired straight to the signal'");

/** @brief strcmp for pin names, without dragging in <cstring> for four lines. */
bool namesEqual(const char* a, const char* b) noexcept {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  while (*a != '\0' && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

}  // namespace

bool HAdcManager::configureAll() noexcept {
  if (kPinCount == 0) {
    return true;
  }

  HIAdc& backend = hAdcBackend();
  bool everything = true;

  for (size_t i = 0; i < kPinCount; ++i) {
    if (!backend.configure(kPins[i])) {
      // Logged by the backend, which knows why. A device with one broken
      // analog input should still be everything else it is.
      everything = false;
    }
  }

  HInfo("%u analog pin(s) configured", static_cast<unsigned>(kPinCount));
  return everything;
}

HAdcChannel HAdcManager::find(const char* name) noexcept {
  for (size_t i = 0; i < kPinCount; ++i) {
    if (namesEqual(kPins[i].name, name)) {
      return HAdcChannel(&kPins[i]);
    }
  }

  HWarning("no analog pin named '%s' - that channel will read nothing",
           (name != nullptr) ? name : "(null)");
  return HAdcChannel();
}

size_t HAdcManager::count() noexcept {
  return kPinCount;
}

HAdcChannel HAdcManager::at(size_t index) noexcept {
  if (index >= kPinCount) {
    return HAdcChannel();
  }
  return HAdcChannel(&kPins[index]);
}
