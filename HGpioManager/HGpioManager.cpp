#include "HGpioManager.hpp"

#define HLOG_MODULE_NAME "HGpio"
#include <HLog/HLog.hpp>

// The board's pin table. Owned by the application, never by the library - see
// the file's own documentation for the row format.
#include <HGpioConfig.h>

// Which backend this is, is decided in HGpioBackend.cpp. The platform headers
// below are still needed here for the pad limits the board table is validated
// against - not for the object itself.
#include "HGpioBackend/HGpioBackend.hpp"

#if IS_MCU

#include <soc/soc_caps.h>

#include "HGpioEsp32/HGpioEsp32.hpp"

#else

#include "HGpioDesktop/HGpioDesktop.hpp"

#endif

namespace {

/** @brief Counts a row without looking at it, so an EMPTY list totals zero. */
#define HGPIO_COUNT_PIN(...) +1

/** @brief Expands one board-table row into a table entry. */
#define HGPIO_MAKE_PIN(name_, number_, dir_, pull_, invert_) \
  HGpioPinDesc{name_, number_, HGpioDir::dir_, HGpioPull::pull_, invert_},

/** @brief Pins compiled in: never read from configuration, never rewritable at runtime. */
constexpr size_t kFixedPinCount = 0 HGPIO_FIXED_PINS(HGPIO_COUNT_PIN);

/** @brief Pins an installer is meant to decide about. See the note on config/gpio.cfg below. */
constexpr size_t kConfigurablePinCount = 0 HGPIO_CONFIGURABLE_PINS(HGPIO_COUNT_PIN);

constexpr size_t kPinCount = kFixedPinCount + kConfigurablePinCount;

/**
 * @brief Storage for the table, never zero-length.
 *
 * A board that declares no pins at all is legal - it simply has none - but a
 * zero-length array is not, so the table keeps one unused row in that case.
 * kPinCount, not this, is what every loop below runs to.
 */
constexpr size_t kTableStorage = (kPinCount > 0) ? kPinCount : 1;

/** @brief Every pin this build serves. Fixed rows first, in declaration order. */
constexpr HGpioPinDesc kPins[kTableStorage] = {
    HGPIO_FIXED_PINS(HGPIO_MAKE_PIN)
    HGPIO_CONFIGURABLE_PINS(HGPIO_MAKE_PIN)
};

#undef HGPIO_COUNT_PIN
#undef HGPIO_MAKE_PIN

// ---------------------------------------------------------------------------
// Compile-time validation of the board table.
//
// Every one of these is a mistake that would otherwise be found by a device
// that boots and then does not work: a pin number that does not exist on this
// chip, an output on a pad that cannot drive, or the same pad claimed twice by
// two names that both think they own it. A build is a much better place to
// find them than a bench.
// ---------------------------------------------------------------------------

#if IS_MCU
constexpr uint64_t kValidMask = static_cast<uint64_t>(SOC_GPIO_VALID_GPIO_MASK);
constexpr uint64_t kValidOutputMask = static_cast<uint64_t>(SOC_GPIO_VALID_OUTPUT_GPIO_MASK);
#else
// The host backend has pads only in the sense that it has an array.
constexpr uint64_t kValidMask = (HGPIO_DESKTOP_PIN_COUNT >= 64)
                                    ? ~0ULL
                                    : ((1ULL << HGPIO_DESKTOP_PIN_COUNT) - 1ULL);
constexpr uint64_t kValidOutputMask = kValidMask;
#endif

/** @brief True if every declared number exists on this chip. */
constexpr bool allNumbersValid() {
  for (size_t i = 0; i < kPinCount; ++i) {
    if (kPins[i].number < 0 || kPins[i].number >= 64) {
      return false;
    }
    if ((kValidMask & (1ULL << kPins[i].number)) == 0ULL) {
      return false;
    }
  }
  return true;
}

/** @brief True if every pin declared as an output sits on a pad that can drive one. */
constexpr bool allOutputsCanDrive() {
  for (size_t i = 0; i < kPinCount; ++i) {
    if (kPins[i].dir == HGpioDir::Output && kPins[i].number >= 0 && kPins[i].number < 64 &&
        (kValidOutputMask & (1ULL << kPins[i].number)) == 0ULL) {
      return false;
    }
  }
  return true;
}

/** @brief True if no pad is claimed by two rows. */
constexpr bool allNumbersUnique() {
  for (size_t i = 0; i < kPinCount; ++i) {
    for (size_t j = i + 1; j < kPinCount; ++j) {
      if (kPins[i].number == kPins[j].number) {
        return false;
      }
    }
  }
  return true;
}

static_assert(allNumbersValid(),
              "HGpioConfig.h declares a GPIO number that does not exist on this chip");
static_assert(allOutputsCanDrive(),
              "HGpioConfig.h declares an Output on a pad that cannot drive one");
static_assert(allNumbersUnique(),
              "HGpioConfig.h claims the same GPIO number twice - two names cannot own one pad");

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

bool HGpioManager::configureAll() noexcept {
  bool ok = true;

  for (size_t i = 0; i < kPinCount; ++i) {
    if (!hGpioBackend().configure(kPins[i])) {
      ok = false;
      continue;
    }
    HDebug("%s -> GPIO%d %s%s", kPins[i].name, kPins[i].number,
           (kPins[i].dir == HGpioDir::Output) ? "out" : "in",
           kPins[i].invert ? " (inverted)" : "");
  }

  HInfo("%u pin(s) configured%s", static_cast<unsigned>(kPinCount), ok ? "" : " - WITH FAILURES");
  return ok;
}

HGpioPin HGpioManager::find(const char* name) noexcept {
  for (size_t i = 0; i < kPinCount; ++i) {
    if (namesEqual(kPins[i].name, name)) {
      return HGpioPin(&kPins[i]);
    }
  }

  // Not fatal, and deliberately loud: the caller gets a handle that reads false
  // and drives nothing, so a typo costs a pin that never acts rather than a
  // crash - but nobody should have to work that out from the symptom.
  HWarning("no pin named '%s' in the board table", (name != nullptr) ? name : "(null)");
  return HGpioPin();
}

size_t HGpioManager::pinCount() noexcept {
  return kPinCount;
}

HGpioPin HGpioManager::at(size_t index) noexcept {
  return (index < kPinCount) ? HGpioPin(&kPins[index]) : HGpioPin();
}

HIGpio& HGpioManager::instance() noexcept {
  return hGpioBackend();
}
