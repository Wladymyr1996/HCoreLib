#include "HCoreLibTest.hpp"

#include <HStatusLed/HRgbLedDesktop/HRgbLedDesktop.hpp>
#include <HStatusLed/HStatusLed.hpp>

/**
 * @file HStatusLedTest.cpp
 * @brief The family's status patterns, to the millisecond, on a fake LED.
 *
 * Driven through HStatusLed::tick() with the time given, never through the
 * real clock, so a pattern is checked at exact instants: the last lit
 * millisecond and the first dark one. HRgbLedDesktop records what was shown.
 */

static_assert(HSTATUSLED_ENABLE, "the test build enables the status LED - see Tests/Config");

namespace {

HRgbLedDesktop& led() {
  return static_cast<HRgbLedDesktop&>(HStatusLed::driver());
}

/** A fixed origin far from zero, and one just before the 32-bit wrap. */
constexpr uint32_t kT0 = 100000;
constexpr uint32_t kNearWrap = 0xFFFFFFFFu - 30;

void reset() noexcept {
  HStatusLed::clearOverlay();
  HStatusLed::setBase(HStatusLedBase::None);
  HStatusLed::tick(0);
}

void testTheTableIsTheFamilyStandard() noexcept {
  const HStatusLedPattern& normal = HStatusLed::pattern(HStatusLedBase::Normal);
  CHECK(normal.onMs == 50 && normal.offMs == 2000);
  CHECK(normal.color.green > 0 && normal.color.red == 0 && normal.color.blue == 0);

  const HStatusLedPattern& configuring = HStatusLed::pattern(HStatusLedBase::Configuring);
  CHECK(configuring.onMs == 50 && configuring.offMs == 2000);
  CHECK(configuring.color.red > 0 && configuring.color.green > 0);

  const HStatusLedPattern& erase = HStatusLed::pattern(HStatusLedBase::FactoryReset);
  CHECK(erase.onMs == 50 && erase.offMs == 50);
  CHECK(erase.color == configuring.color);

  const HStatusLedPattern& failed = HStatusLed::pattern(HStatusLedBase::Failed);
  CHECK(failed.onMs == 1000 && failed.offMs == 500);
  CHECK(failed.color.red > 0 && failed.color.green == 0 && failed.color.blue == 0);
  CHECK(HStatusLed::isLit(failed, 999));
  CHECK(!HStatusLed::isLit(failed, 1000));
  CHECK(!HStatusLed::isLit(failed, 1499));
  CHECK(HStatusLed::isLit(failed, 1500));

  // Normal's heartbeat, in Failed's colour.
  const HStatusLedPattern& degraded = HStatusLed::pattern(HStatusLedBase::Degraded);
  CHECK(degraded.onMs == normal.onMs && degraded.offMs == normal.offMs);
  CHECK(degraded.color == failed.color);

  const HStatusLedPattern& slave = HStatusLed::pattern(HStatusLedOverlay::BindSlave);
  CHECK(slave.onMs == 200 && slave.offMs == 200);
  CHECK(slave.color == configuring.color);

  // Solid: lit at every instant.
  const HStatusLedPattern& master = HStatusLed::pattern(HStatusLedOverlay::BindMaster);
  CHECK(master.offMs == 0);
  CHECK(HStatusLed::isLit(master, 0));
  CHECK(HStatusLed::isLit(master, 123456));

  CHECK(!HStatusLed::isLit(HStatusLed::pattern(HStatusLedBase::None), 0));
}

void testABlinkIsLitFirstThenDark() noexcept {
  const HStatusLedPattern& normal = HStatusLed::pattern(HStatusLedBase::Normal);

  CHECK(HStatusLed::isLit(normal, 0));
  CHECK(HStatusLed::isLit(normal, 49));
  CHECK(!HStatusLed::isLit(normal, 50));
  CHECK(!HStatusLed::isLit(normal, 2049));
  CHECK(HStatusLed::isLit(normal, 2050));  // the next period
}

void testBrightnessScalesButNeverExtinguishes() noexcept {
  const HStatusLedColor full = {255, 255, 255};
  const HStatusLedColor scaled = HStatusLed::scale(full);
  CHECK(scaled.red == HSTATUSLED_BRIGHTNESS);

  // A faint channel must survive, or yellow turns red at low brightness.
  const HStatusLedColor faint = HStatusLed::scale(HStatusLedColor{255, 1, 0});
  CHECK(faint.green == 1);
  CHECK(faint.blue == 0);
}

void testNormalBlipsOnTheDevice() noexcept {
  reset();
  CHECK(HStatusLed::begin());

  HStatusLed::setBase(HStatusLedBase::Normal);

  HStatusLed::tick(kT0);
  CHECK(led().isLit());
  CHECK(led().green() > 0 && led().red() == 0);

  HStatusLed::tick(kT0 + 49);
  CHECK(led().isLit());

  HStatusLed::tick(kT0 + 50);
  CHECK(!led().isLit());

  HStatusLed::tick(kT0 + 2050);
  CHECK(led().isLit());
}

void testTheLedIsOnlyWrittenOnAChange() noexcept {
  reset();
  HStatusLed::setBase(HStatusLedBase::Normal);
  HStatusLed::tick(kT0);

  const uint32_t before = led().writes();

  // Two seconds of 10 ms ticks: one change to dark, one back to lit.
  for (uint32_t t = kT0 + 10; t <= kT0 + 2050; t += 10) {
    HStatusLed::tick(t);
  }

  CHECK(led().writes() - before == 2);
}

void testAnOverlayReplacesTheBaseAndClearingBringsItBack() noexcept {
  reset();
  HStatusLed::setBase(HStatusLedBase::Normal);
  HStatusLed::tick(kT0);

  // Slave attempt: yellow 200/200, starting lit.
  HStatusLed::setOverlay(HStatusLedOverlay::BindSlave);
  HStatusLed::tick(kT0 + 500);
  CHECK(led().isLit() && led().red() > 0);
  HStatusLed::tick(kT0 + 700);
  CHECK(!led().isLit());
  HStatusLed::tick(kT0 + 900);
  CHECK(led().isLit());

  // Master window: solid, however long.
  HStatusLed::setOverlay(HStatusLedOverlay::BindMaster);
  HStatusLed::tick(kT0 + 1000);
  CHECK(led().isLit() && led().red() > 0);
  HStatusLed::tick(kT0 + 60000);
  CHECK(led().isLit());

  // Cleared: the green blip is back, restarted, so lit first.
  HStatusLed::clearOverlay();
  CHECK(HStatusLed::overlay() == HStatusLedOverlay::None);
  HStatusLed::tick(kT0 + 70000);
  CHECK(led().isLit() && led().green() > 0 && led().red() == 0);
  HStatusLed::tick(kT0 + 70050);
  CHECK(!led().isLit());
}

void testSettingTheSameStatusDoesNotRestartIt() noexcept {
  reset();
  HStatusLed::setBase(HStatusLedBase::FactoryReset);
  HStatusLed::tick(kT0);
  HStatusLed::tick(kT0 + 60);
  CHECK(!led().isLit());  // 50 on / 50 off: dark at 60

  // A restart would make it lit again at the next tick.
  HStatusLed::setBase(HStatusLedBase::FactoryReset);
  HStatusLed::tick(kT0 + 70);
  CHECK(!led().isLit());
}

void testABaseChangeUnderAnOverlayIsNotVisible() noexcept {
  reset();
  HStatusLed::setOverlay(HStatusLedOverlay::BindMaster);
  HStatusLed::tick(kT0);
  HStatusLed::setBase(HStatusLedBase::Configuring);
  HStatusLed::tick(kT0 + 5000);
  CHECK(led().isLit());  // still the solid overlay
  CHECK(HStatusLed::base() == HStatusLedBase::Configuring);
}

void testThePatternSurvivesTheClockWrap() noexcept {
  reset();
  HStatusLed::setBase(HStatusLedBase::FactoryReset);
  HStatusLed::tick(kNearWrap);
  CHECK(led().isLit());

  // 60 ms later the clock has wrapped past zero.
  HStatusLed::tick(kNearWrap + 60);
  CHECK(!led().isLit());
  HStatusLed::tick(kNearWrap + 100);
  CHECK(led().isLit());
}

}  // namespace

void runStatusLedTests() noexcept {
  testTheTableIsTheFamilyStandard();
  testABlinkIsLitFirstThenDark();
  testBrightnessScalesButNeverExtinguishes();
  testNormalBlipsOnTheDevice();
  testTheLedIsOnlyWrittenOnAChange();
  testAnOverlayReplacesTheBaseAndClearingBringsItBack();
  testSettingTheSameStatusDoesNotRestartIt();
  testABaseChangeUnderAnOverlayIsNotVisible();
  testThePatternSurvivesTheClockWrap();
}
