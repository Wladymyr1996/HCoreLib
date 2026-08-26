#include "HGpioManager.hpp"

#define HLOG_MODULE_NAME "HGpio"
#include <HLog/HLog.hpp>

// The board's pin table. Owned by the application, never by the library - see
// the file's own documentation for the row format.
#include <HGpioConfig.h>

// A board table written before analog pins existed declares no such list, and
// must still build. An empty list costs this file nothing.
#ifndef HGPIO_ANALOG_PINS
#define HGPIO_ANALOG_PINS(PIN)
#endif

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

/**
 * @brief The board table as DECLARED. Compile-time, and the checks below run on it.
 */
constexpr HGpioPinDesc kPins[kTableStorage] = {
    HGPIO_FIXED_PINS(HGPIO_MAKE_PIN)
    HGPIO_CONFIGURABLE_PINS(HGPIO_MAKE_PIN)
};

/**
 * @brief The table as it is RUNNING. What find() hands out pointers into.
 *
 * A second copy, and the initialiser is expanded twice rather than copied from
 * kPins at start-up, because find() has to work before anything has run - a
 * table filled in by an init() nobody remembered to call is a device whose pins
 * all read false.
 *
 * The two differ only for CONFIGURABLE rows, and only after setPin(): direction
 * and inversion on those are an installer's decision rather than a property of
 * the enclosure, so they arrive from configuration before configureAll() and are
 * written here. Fixed rows are never touched.
 *
 * HGpioPin still holds a `const HGpioPinDesc*` into this, and the guarantee its
 * header depends on is unchanged in the way that matters: the row is STATIC and
 * outlives the program, so a handle can never dangle. What it is no longer is
 * immutable - and since every write happens before configureAll(), no handle can
 * observe one mid-flight.
 */
HGpioPinDesc gPins[kTableStorage] = {
    HGPIO_FIXED_PINS(HGPIO_MAKE_PIN)
    HGPIO_CONFIGURABLE_PINS(HGPIO_MAKE_PIN)
};

/**
 * @brief One ANALOG row reduced to its pad number, and nothing else.
 *
 * This module knows nothing about attenuation, dividers or converters - that is
 * HAdcManager's, and it does not include a single header of it. What it does
 * know, and what nothing else can, is that a pad has ONE owner: the analog rows
 * are declared in the same board table precisely so this check can see them
 * beside the digital ones.
 *
 * A board with no analog pins expands to nothing and costs this file nothing.
 */
#define HGPIO_ANALOG_NUMBER(name_, number_, atten_, divider_) number_,

constexpr size_t kAnalogPinCount = 0 HGPIO_ANALOG_PINS(HGPIO_COUNT_PIN);

constexpr size_t kAnalogStorage = (kAnalogPinCount > 0) ? kAnalogPinCount : 1;

constexpr int kAnalogNumbers[kAnalogStorage] = {HGPIO_ANALOG_PINS(HGPIO_ANALOG_NUMBER)};

#undef HGPIO_COUNT_PIN
#undef HGPIO_MAKE_PIN
#undef HGPIO_ANALOG_NUMBER

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

/**
 * @brief True if no pad is claimed by two rows, in ANY of the three lists.
 *
 * Spanning all three is the whole reason the analog pins are declared in the
 * same board table. gpio_config() on a pad the converter owns tears the ADC off
 * it, and the reverse is just as true - so "the same pad twice" has to mean
 * across every list, not within each one.
 */
constexpr bool allNumbersUnique() {
  for (size_t i = 0; i < kPinCount; ++i) {
    for (size_t j = i + 1; j < kPinCount; ++j) {
      if (kPins[i].number == kPins[j].number) {
        return false;
      }
    }
  }

  for (size_t i = 0; i < kAnalogPinCount; ++i) {
    for (size_t j = i + 1; j < kAnalogPinCount; ++j) {
      if (kAnalogNumbers[i] == kAnalogNumbers[j]) {
        return false;
      }
    }

    for (size_t j = 0; j < kPinCount; ++j) {
      if (kAnalogNumbers[i] == kPins[j].number) {
        return false;
      }
    }
  }

  return true;
}

/**
 * @brief True if every CONFIGURABLE pad could serve as an output.
 *
 * Stronger than allOutputsCanDrive() and for a reason that only exists now that
 * direction is an installer's decision: a row declared Input may be set to
 * Output at runtime, so the pad behind it has to be able to drive one whatever
 * the table says today. Checking it here turns "this port silently does nothing"
 * into a build error on a board whose pad map is wrong.
 */
constexpr bool allConfigurableCanDrive() {
  for (size_t i = kFixedPinCount; i < kPinCount; ++i) {
    if (kPins[i].number < 0 || kPins[i].number >= 64) {
      return false;
    }
    if ((kValidOutputMask & (1ULL << kPins[i].number)) == 0ULL) {
      return false;
    }
  }
  return true;
}

static_assert(allConfigurableCanDrive(),
              "HGpioConfig.h puts a CONFIGURABLE pin on a pad that cannot drive an "
              "output - and a configurable pin may be set to one at runtime");

static_assert(allNumbersValid(),
              "HGpioConfig.h declares a GPIO number that does not exist on this chip");
static_assert(allOutputsCanDrive(),
              "HGpioConfig.h declares an Output on a pad that cannot drive one");
static_assert(allNumbersUnique(),
              "HGpioConfig.h claims the same GPIO number twice - two names cannot own one pad. "
              "This spans the fixed, configurable AND analog lists: a digital pin and an "
              "analog one cannot share a pad either");

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
    if (!hGpioBackend().configure(gPins[i])) {
      ok = false;
      continue;
    }
    HDebug("%s -> GPIO%d %s%s%s", gPins[i].name, gPins[i].number,
           (gPins[i].dir == HGpioDir::Output) ? "out" : "in",
           gPins[i].invert ? " (inverted)" : "",
           (i >= kFixedPinCount) ? " [configurable]" : "");
  }

  HInfo("%u pin(s) configured%s", static_cast<unsigned>(kPinCount), ok ? "" : " - WITH FAILURES");
  return ok;
}

HGpioPin HGpioManager::find(const char* name) noexcept {
  for (size_t i = 0; i < kPinCount; ++i) {
    if (namesEqual(gPins[i].name, name)) {
      return HGpioPin(&gPins[i]);
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
  return (index < kPinCount) ? HGpioPin(&gPins[index]) : HGpioPin();
}

bool HGpioManager::isConfigurable(const char* name) noexcept {
  for (size_t i = kFixedPinCount; i < kPinCount; ++i) {
    if (namesEqual(gPins[i].name, name)) {
      return true;
    }
  }

  return false;
}

bool HGpioManager::setPin(const char* name, HGpioDir dir, bool invert) noexcept {
  for (size_t i = 0; i < kPinCount; ++i) {
    if (!namesEqual(gPins[i].name, name)) {
      continue;
    }

    // A FIXED pin is a property of the enclosure: both buttons are soldered to
    // one pad each on every unit that will ever be built. A configuration that
    // could re-point one would be a way to brick a sealed device from a web
    // form, which is the whole reason the two lists exist.
    if (i < kFixedPinCount) {
      HWarning("'%s' is a fixed pin and cannot be reconfigured", name);
      return false;
    }

    gPins[i].dir = dir;
    gPins[i].invert = invert;
    return true;
  }

  HWarning("no pin named '%s' to configure", (name != nullptr) ? name : "(null)");
  return false;
}

size_t HGpioManager::fixedPinCount() noexcept {
  return kFixedPinCount;
}

HIGpio& HGpioManager::instance() noexcept {
  return hGpioBackend();
}
