#include "HStatusLed.hpp"

#if HSTATUSLED_ENABLE

#include <HSystemUtils/HSystemUtils.hpp>

#include "HIRgbLed/HIRgbLed.hpp"

#if IS_MCU
#include "HRgbLedEsp32/HRgbLedEsp32.hpp"
#else
#include "HRgbLedDesktop/HRgbLedDesktop.hpp"
#endif

static_assert(HSTATUSLED_BRIGHTNESS >= 1 && HSTATUSLED_BRIGHTNESS <= 255,
              "HSTATUSLED_BRIGHTNESS is out of 255 - 0 would be a status LED that never lights");

namespace {

// ---------------------------------------------------------------------------
// The standard patterns. README.md carries the same table; the two must agree.
// ---------------------------------------------------------------------------

constexpr HStatusLedColor kDark = {0, 0, 0};
constexpr HStatusLedColor kGreen = {0, 255, 0};
constexpr HStatusLedColor kRed = {255, 0, 0};

// Not 255/255/0: on an RGB LED equal red and green read as lime. Less green
// reads as yellow on the parts this family has seen.
constexpr HStatusLedColor kYellow = {255, 160, 0};

/** A heartbeat blip: short enough to be unmistakably "a blink", 2 s apart. */
constexpr uint16_t kBlipMs = 50;
constexpr uint16_t kBlipGapMs = 2000;

constexpr HStatusLedPattern kOff = {kDark, 0, 0};

constexpr HStatusLedPattern kNormal = {kGreen, kBlipMs, kBlipGapMs};
constexpr HStatusLedPattern kConfiguring = {kYellow, kBlipMs, kBlipGapMs};
constexpr HStatusLedPattern kFactoryReset = {kYellow, 50, 50};

// Long on, short off: the only pattern that is mostly LIT, so it cannot be
// mistaken for a heartbeat at a glance - and red, which nothing else uses.
constexpr HStatusLedPattern kFailed = {kRed, 1000, 500};

constexpr HStatusLedPattern kBindSlave = {kYellow, 200, 200};
constexpr HStatusLedPattern kBindMaster = {kYellow, 1, 0};  // off 0: solid

// ---------------------------------------------------------------------------

HStatusLedBase gBase = HStatusLedBase::None;
HStatusLedOverlay gOverlay = HStatusLedOverlay::None;

/** True when the status changed and the pattern must start over on the next tick. */
bool gRestart = true;
uint32_t gStartedAtMs = 0;

bool gReady = false;
bool gHasShown = false;
HStatusLedColor gShown = kDark;

void show(const HStatusLedColor& color) noexcept {
  if (gHasShown && color == gShown) {
    return;  // Unchanged: nothing to send, and RMT is not free.
  }

  gShown = color;
  gHasShown = true;
  HStatusLed::driver().show(color.red, color.green, color.blue);
}

}  // namespace

bool HStatusLed::begin() noexcept {
  if (gReady) {
    return true;
  }

  gReady = driver().begin();
  if (gReady) {
    gHasShown = false;
    show(kDark);
  }

  return gReady;
}

void HStatusLed::setBase(HStatusLedBase base) noexcept {
  if (base == gBase) {
    return;
  }

  gBase = base;

  // The base is only visible with no overlay; restarting it under one would
  // restart nothing anybody can see.
  if (gOverlay == HStatusLedOverlay::None) {
    gRestart = true;
  }
}

void HStatusLed::setOverlay(HStatusLedOverlay overlay) noexcept {
  if (overlay == gOverlay) {
    return;
  }

  gOverlay = overlay;
  gRestart = true;
}

void HStatusLed::clearOverlay() noexcept {
  setOverlay(HStatusLedOverlay::None);
}

HStatusLedBase HStatusLed::base() noexcept {
  return gBase;
}

HStatusLedOverlay HStatusLed::overlay() noexcept {
  return gOverlay;
}

void HStatusLed::update() noexcept {
  tick(HSystemUtils::millis());
}

void HStatusLed::tick(uint32_t nowMs) noexcept {
  if (!gReady) {
    return;
  }

  if (gRestart) {
    gRestart = false;
    gStartedAtMs = nowMs;
  }

  const HStatusLedPattern& current =
      gOverlay != HStatusLedOverlay::None ? pattern(gOverlay) : pattern(gBase);

  // Unsigned subtraction: correct across the millis() wrap.
  show(isLit(current, nowMs - gStartedAtMs) ? scale(current.color) : kDark);
}

void HStatusLed::runFor(uint32_t durationMs) noexcept {
  const uint32_t startedAt = HSystemUtils::millis();

  while (HSystemUtils::millis() - startedAt < durationMs) {
    update();
    HSystemUtils::sleep(HCORELIB_TICK_MS);
  }
}

const HStatusLedPattern& HStatusLed::pattern(HStatusLedBase base) noexcept {
  switch (base) {
    case HStatusLedBase::Normal:
      return kNormal;
    case HStatusLedBase::Configuring:
      return kConfiguring;
    case HStatusLedBase::FactoryReset:
      return kFactoryReset;
    case HStatusLedBase::Failed:
      return kFailed;
    case HStatusLedBase::None:
      break;
  }

  return kOff;
}

const HStatusLedPattern& HStatusLed::pattern(HStatusLedOverlay overlay) noexcept {
  switch (overlay) {
    case HStatusLedOverlay::BindSlave:
      return kBindSlave;
    case HStatusLedOverlay::BindMaster:
      return kBindMaster;
    case HStatusLedOverlay::None:
      break;
  }

  return kOff;
}

bool HStatusLed::isLit(const HStatusLedPattern& pattern, uint32_t elapsedMs) noexcept {
  if (pattern.onMs == 0) {
    return false;
  }

  if (pattern.offMs == 0) {
    return true;
  }

  const uint32_t period = static_cast<uint32_t>(pattern.onMs) + pattern.offMs;
  return elapsedMs % period < pattern.onMs;
}

HStatusLedColor HStatusLed::scale(const HStatusLedColor& color) noexcept {
  const auto one = [](uint8_t channel) noexcept -> uint8_t {
    if (channel == 0) {
      return 0;
    }

    const uint32_t scaled = (static_cast<uint32_t>(channel) * HSTATUSLED_BRIGHTNESS + 127U) / 255U;

    // Never round a lit channel down to nothing: at a low brightness that
    // would turn yellow into red, which is a different status.
    return scaled == 0 ? 1 : static_cast<uint8_t>(scaled);
  };

  return HStatusLedColor{one(color.red), one(color.green), one(color.blue)};
}

HIRgbLed& HStatusLed::driver() noexcept {
  // Function-local, for the same reason as hGpioBackend(): a static
  // constructor elsewhere must not race this object's own construction.
#if IS_MCU
  static HRgbLedEsp32 led;
#else
  static HRgbLedDesktop led;
#endif
  return led;
}

#endif  // HSTATUSLED_ENABLE
