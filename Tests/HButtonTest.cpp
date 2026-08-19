#include "HCoreLibTest.hpp"

#include <HButton/HButton.hpp>
#include <HGpioManager/HGpioDesktop/HGpioDesktop.hpp>
#include <HGpioManager/HGpioManager.hpp>
#include <HSystemUtils/HSystemUtils.hpp>

/**
 * @file HButtonTest.cpp
 * @brief A real debounce, driven through a synthetic bounce train.
 *
 * This is what the desktop GPIO backend is for. The pad is an array cell, so a
 * test can bounce it twenty times in fifteen milliseconds the way a tactile
 * switch does, and the code doing the debouncing is byte for byte the code that
 * runs on the device.
 *
 * The timings are scaled down so the suite stays fast, but not as far down as
 * they would go if the host's clock were the device's. Windows wakes a sleeping
 * thread on a ~15 ms timer tick, so a "2 ms" sleep is anything up to 16 ms -
 * which is why the windows below are far enough apart that a handful of missed
 * ticks cannot move a press from one side of a threshold to the other, why the
 * tick loop is driven by millis() rather than by counting iterations, and why
 * the bounce train does not sleep at all.
 *
 * Every assertion is about ORDER and COUNT rather than about a duration to the
 * millisecond: a CI runner that loses the scheduler for a moment must not turn
 * a working debounce into a red build.
 */

namespace {

constexpr uint32_t kDebounceMs = 20;
constexpr uint32_t kLongPressMs = 300;

/** @brief Long enough to settle a level, short enough not to be a long press. */
constexpr uint32_t kSettleMs = 120;

/** @brief The pad the board table gives "btn". Wired to ground, so LOW = pressed. */
int gButtonPad = -1;

int gPressed = 0;
int gLongPressed = 0;
int gReleased = 0;
uint32_t gHeldMs = 0;

void onPressed() { ++gPressed; }
void onLongPressed() { ++gLongPressed; }
void onReleased(uint32_t heldMs) {
  ++gReleased;
  gHeldMs = heldMs;
}

HGpioDesktop& gpio() {
  return static_cast<HGpioDesktop&>(HGpioManager::instance());
}

/** @brief What the switch does: LOW is pressed, because the row says invert. */
void setPressed(bool pressed) {
  gpio().setRawLevel(gButtonPad, !pressed);
}

/**
 * @brief Runs the tick loop for `ms`, the way an application's task would.
 *
 * Bounded by the CLOCK, not by a count of sleeps: the host may hand back far
 * more than the millisecond that was asked for, and a loop that assumed
 * otherwise would run for several times the duration it claims to.
 */
void pump(HButton& button, uint32_t ms) {
  const uint32_t startedAt = HSystemUtils::millis();
  do {
    button.update();
    HSystemUtils::sleep(1);
  } while (HSystemUtils::millis() - startedAt < ms);
  button.update();
}

void resetCounters() {
  gPressed = 0;
  gLongPressed = 0;
  gReleased = 0;
  gHeldMs = 0;
}

/** @brief Builds a button whose pin is already released and settled. */
HButton makeButton() {
  setPressed(false);
  HButton button(HGpioManager::find("btn"), kDebounceMs, kLongPressMs);
  button.onPressed(HButtonCallback::create<&onPressed>());
  button.onLongPressed(HButtonCallback::create<&onLongPressed>());
  button.onReleased(HButtonReleasedCallback::create<&onReleased>());
  return button;
}

/**
 * @brief Contact bounce produces no events at all, not a burst of them.
 *
 * A disagreement that does not survive the debounce window is exactly what
 * bounce is, so the level never settles and nothing is ever believed.
 */
void checkBounceIsIgnored() {
  resetCounters();
  HButton button = makeButton();
  pump(button, kSettleMs);
  resetCounters();

  // A real contact chatters for a few milliseconds, so the train is driven as
  // fast as the loop runs and stopped by the clock - sleeping between edges
  // would hand the host's 15 ms timer enough time to settle the level and turn
  // this into an ordinary press.
  const uint32_t startedAt = HSystemUtils::millis();
  bool level = false;
  while (HSystemUtils::millis() - startedAt < kDebounceMs / 2) {
    level = !level;
    setPressed(level);
    button.update();
  }

  setPressed(false);
  pump(button, kSettleMs);

  CHECK(gPressed == 0);
  CHECK(gReleased == 0);
  CHECK(!button.isPressed());
}

void checkShortPress() {
  resetCounters();
  HButton button = makeButton();
  pump(button, kSettleMs);
  resetCounters();

  setPressed(true);
  pump(button, kSettleMs);

  CHECK(gPressed == 1);
  CHECK(button.isPressed());
  CHECK(gLongPressed == 0);
  CHECK(gReleased == 0);

  setPressed(false);
  pump(button, kSettleMs);

  CHECK(gPressed == 1);      // and not a second time
  CHECK(gReleased == 1);
  CHECK(!button.isPressed());
  CHECK(gHeldMs >= kDebounceMs);
  CHECK(gHeldMs < kLongPressMs);
}

/**
 * @brief The long press fires ONCE, while the button is still down.
 *
 * That is what makes "hold three seconds to reset" feel right: the device acts
 * and the user lets go afterwards - so the release still arrives, carrying the
 * full duration.
 */
void checkLongPress() {
  resetCounters();
  HButton button = makeButton();
  pump(button, kSettleMs);
  resetCounters();

  setPressed(true);
  pump(button, kDebounceMs + kLongPressMs + kSettleMs);

  CHECK(gPressed == 1);
  CHECK(gLongPressed == 1);
  CHECK(button.isPressed());
  CHECK(gReleased == 0);

  // Holding longer does not fire it again.
  pump(button, kSettleMs);
  CHECK(gLongPressed == 1);

  setPressed(false);
  pump(button, kSettleMs);

  CHECK(gReleased == 1);
  CHECK(gHeldMs >= kLongPressMs);
}

/** @brief Two presses in a row are two of everything, with the long press re-armed. */
void checkRepeatedPresses() {
  resetCounters();
  HButton button = makeButton();
  pump(button, kSettleMs);
  resetCounters();

  for (int i = 0; i < 2; ++i) {
    setPressed(true);
    pump(button, kDebounceMs + kLongPressMs + kSettleMs);
    setPressed(false);
    pump(button, kSettleMs);
  }

  CHECK(gPressed == 2);
  CHECK(gLongPressed == 2);
  CHECK(gReleased == 2);
}

/**
 * @brief A button already down when the object is built reports no press.
 *
 * A device woken BY the button would otherwise announce a press nobody made.
 * The long press and the release that follow are still reported, because those
 * did happen while somebody was watching.
 */
void checkAdoptsInitialLevel() {
  resetCounters();
  setPressed(true);

  HButton button(HGpioManager::find("btn"), kDebounceMs, kLongPressMs);
  button.onPressed(HButtonCallback::create<&onPressed>());
  button.onReleased(HButtonReleasedCallback::create<&onReleased>());

  CHECK(button.isPressed());
  pump(button, kSettleMs);
  CHECK(gPressed == 0);

  setPressed(false);
  pump(button, kSettleMs);
  CHECK(gReleased == 1);
}

/** @brief A handle naming no pin never reports anything, and never crashes. */
void checkInvalidPin() {
  resetCounters();

  HButton button(HGpioPin(), kDebounceMs, kLongPressMs);
  button.onPressed(HButtonCallback::create<&onPressed>());
  pump(button, kSettleMs);

  CHECK(gPressed == 0);
  CHECK(!button.isPressed());
}

}  // namespace

void runButtonTests() noexcept {
  HCoreLibTest::begin("HButton");

  HGpioManager::configureAll();
  gButtonPad = HGpioManager::find("btn").number();
  REQUIRE(gButtonPad >= 0);

  checkBounceIsIgnored();
  checkShortPress();
  checkLongPress();
  checkRepeatedPresses();
  checkAdoptsInitialLevel();
  checkInvalidPin();

  setPressed(false);
}
