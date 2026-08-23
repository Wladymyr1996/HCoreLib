#include "HCoreLibTest.hpp"

#include <HAdcManager/HAdcManager.hpp>
#include <HAdcManager/HAdcBackend/HAdcBackend.hpp>
#include <HAdcManager/HAdcDesktop/HAdcDesktop.hpp>
#include <HCurve/HCurve.hpp>

/**
 * @file HAdcTest.cpp
 * @brief The divider arithmetic, the median, and a curve read off a table.
 *
 * None of this needs a converter, which is the whole reason HIAdc has a desktop
 * backend: a battery gauge can be walked from a full pack to a flat one, one
 * millivolt at a time, on a machine with no board attached - including the two
 * ends that a bench supply makes awkward and a real battery makes slow.
 *
 * The test board's table (Tests/Config/HGpioConfig.h) declares no analog pins,
 * which is itself worth checking: most boards have none, and a library that
 * only works on the boards that do is a library with a hole in it.
 */

namespace {

/** @brief The host backend, reached the same way HAdcChannel reaches it. */
HAdcDesktop& backend() noexcept {
  return static_cast<HAdcDesktop&>(hAdcBackend());
}

/** @brief A row the manager never sees, so a channel can be built by hand. */
constexpr HAdcPinDesc kBattery = {"vbat", 3, HAdcAtten::Atten12db, 2.0f};
constexpr HAdcPinDesc kDirect = {"raw", 4, HAdcAtten::Atten12db, 1.0f};

void checkEmptyTable() {
  // This board declares no analog pins at all, and everything must still work.
  CHECK(HAdcManager::count() == 0);
  CHECK(HAdcManager::configureAll());

  // An unknown name yields a channel that is safe to use and reads nothing.
  const HAdcChannel missing = HAdcManager::find("nosuchpin");
  CHECK(!missing.isValid());
  CHECK(missing.readMv() == -1);
  CHECK(missing.readPadMv() == -1);
  CHECK(missing.readRaw() == -1);
  CHECK(missing.number() == -1);
  CHECK_TEXT(missing.name(), "");

  // A default-constructed one behaves the same, and does not dereference null.
  const HAdcChannel none;
  CHECK(!none.isValid());
  CHECK(none.readMv() == -1);
  CHECK_TEXT(none.name(), "");

  CHECK(!HAdcManager::at(0).isValid());
}

void checkDividerIsUndone() {
  backend().reset();
  REQUIRE(backend().configure(kBattery));

  const HAdcChannel battery(&kBattery);
  REQUIRE(battery.isValid());
  CHECK_TEXT(battery.name(), "vbat");

  // 2.25 V at the pad behind a 2.0 divider IS a 4.5 V pack. Nothing above the
  // channel should ever see the 2250.
  backend().setMv(kBattery.number, 2250);
  CHECK(battery.readPadMv() == 2250);
  CHECK(battery.readMv() == 4500);

  backend().setMv(kBattery.number, 1350);
  CHECK(battery.readMv() == 2700);

  // A pin wired straight to the signal reports what the pad saw.
  REQUIRE(backend().configure(kDirect));
  const HAdcChannel direct(&kDirect);
  backend().setMv(kDirect.number, 1234);
  CHECK(direct.readMv() == 1234);
  CHECK(direct.readPadMv() == 1234);
}

/**
 * @brief The whole battery range, which is what the desktop backend is for.
 *
 * Every 10 mV from a flat pack to a fresh one, checking the divider never
 * rounds its way outside a millivolt of the truth and never clips.
 */
void checkFullRangeSweep() {
  backend().reset();
  REQUIRE(backend().configure(kBattery));
  const HAdcChannel battery(&kBattery);

  for (int packMv = 2700; packMv <= 4950; packMv += 10) {
    backend().setMv(kBattery.number, packMv / 2);

    const int reported = battery.readMv();
    const int error = (reported > packMv) ? (reported - packMv) : (packMv - reported);

    // One millivolt of slack for the halving above, which loses the odd bit.
    if (error > 1) {
      CHECK(error <= 1);
      return;  // One failure is the finding; 225 of them are noise.
    }
  }

  CHECK(true);
}

void checkFailedReadsAreNotZero() {
  backend().reset();
  REQUIRE(backend().configure(kBattery));
  const HAdcChannel battery(&kBattery);

  backend().setMv(kBattery.number, 2250);
  CHECK(battery.readMv() == 4500);

  // A converter that has stopped answering must report NOTHING, not a flat
  // pack. This is the single most important behaviour in the module: a
  // plausible zero here would be read as an empty battery and would put a
  // device into its low-power sulk on the strength of a broken ADC.
  backend().setFailing(true);
  CHECK(battery.readMv() == -1);
  CHECK(battery.readPadMv() == -1);
  CHECK(battery.readRaw() == -1);

  backend().setFailing(false);
  CHECK(battery.readMv() == 4500);

  // An unconfigured pad is the same answer.
  backend().reset();
  CHECK(battery.readMv() == -1);
}

void checkRawIsNotMillivolts() {
  backend().reset();
  REQUIRE(backend().configure(kDirect));
  const HAdcChannel direct(&kDirect);

  backend().setMv(kDirect.number, 1550);

  // Counts, not millivolts - the two paths are genuinely different, and a
  // backend that returned the same number for both would hide a real mistake.
  const int raw = direct.readRaw();
  CHECK(raw > 0);
  CHECK(raw != 1550);
  CHECK(raw <= 4095);
}

/** @brief A zero divider is a mistyped table and must not read as a flat source. */
void checkBadDividerIsRefused() {
  constexpr HAdcPinDesc kBroken = {"broken", 5, HAdcAtten::Atten12db, 0.0f};

  backend().reset();
  REQUIRE(backend().configure(kBroken));
  backend().setMv(kBroken.number, 1500);

  const HAdcChannel broken(&kBroken);

  // The pad reading is real and is reported; the SOURCE reading is impossible
  // and is refused rather than returned as 0, which would read as flat.
  CHECK(broken.readPadMv() == 1500);
  CHECK(broken.readMv() == -1);
}

// ---------------------------------------------------------------------------
// HCurve - the piecewise-linear read a battery gauge is built on
// ---------------------------------------------------------------------------

/** The alkaline 3S curve this project ships, in pack millivolts against percent. */
constexpr HCurvePoint kAlkaline3S[] = {
    {2700.0f, 0.0f},  {3300.0f, 7.0f},  {3600.0f, 20.0f}, {3900.0f, 42.0f},
    {4200.0f, 65.0f}, {4500.0f, 85.0f}, {4800.0f, 100.0f},
};

constexpr size_t kAlkalineCount = sizeof(kAlkaline3S) / sizeof(kAlkaline3S[0]);

static_assert(HCurve::isSorted(kAlkaline3S, kAlkalineCount),
              "the alkaline curve must be sorted ascending by voltage");

void checkCurveKnees() {
  // Every knee reads back exactly, or the table is not the curve.
  for (size_t i = 0; i < kAlkalineCount; ++i) {
    CHECK_NEAR(HCurve::at(kAlkaline3S, kAlkalineCount, kAlkaline3S[i].x), kAlkaline3S[i].y, 0.01f);
  }
}

void checkCurveInterpolates() {
  // Halfway between two knees is halfway between their values.
  CHECK_NEAR(HCurve::at(kAlkaline3S, kAlkalineCount, 4350.0f), 75.0f, 0.01f);
  CHECK_NEAR(HCurve::at(kAlkaline3S, kAlkalineCount, 3750.0f), 31.0f, 0.01f);
}

void checkCurveClamps() {
  // Past either end returns that end, not a line continued into nonsense. A
  // pack reading 5.2 V is a measurement or a divider to doubt - it is not 130%.
  CHECK_NEAR(HCurve::at(kAlkaline3S, kAlkalineCount, 5200.0f), 100.0f, 0.01f);
  CHECK_NEAR(HCurve::at(kAlkaline3S, kAlkalineCount, 1000.0f), 0.0f, 0.01f);
  CHECK_NEAR(HCurve::at(kAlkaline3S, kAlkalineCount, 0.0f), 0.0f, 0.01f);
}

void checkCurveIsMonotonic() {
  // Charge may never rise as the pack falls. A table somebody edits later can
  // break this without breaking anything that looks like a test.
  float previous = -1.0f;
  for (int mv = 2600; mv <= 5000; mv += 25) {
    const float percent = HCurve::at(kAlkaline3S, kAlkalineCount, static_cast<float>(mv));
    if (percent < previous) {
      CHECK(percent >= previous);
      return;
    }
    previous = percent;
  }
  CHECK(true);
}

void checkCurveDegenerates() {
  CHECK_NEAR(HCurve::at(nullptr, 0, 100.0f), 0.0f, 0.01f);
  CHECK_NEAR(HCurve::at(kAlkaline3S, 0, 100.0f), 0.0f, 0.01f);

  // One point is a constant, which is the only sensible reading of a table
  // with nothing to interpolate between.
  constexpr HCurvePoint kOne[] = {{100.0f, 42.0f}};
  CHECK_NEAR(HCurve::at(kOne, 1, 0.0f), 42.0f, 0.01f);
  CHECK_NEAR(HCurve::at(kOne, 1, 1000.0f), 42.0f, 0.01f);

  // isSorted() catches what a static_assert should have caught earlier.
  constexpr HCurvePoint kBackwards[] = {{10.0f, 0.0f}, {5.0f, 1.0f}};
  CHECK(!HCurve::isSorted(kBackwards, 2));

  constexpr HCurvePoint kRepeated[] = {{10.0f, 0.0f}, {10.0f, 1.0f}};
  CHECK(!HCurve::isSorted(kRepeated, 2));
}

}  // namespace

void runAdcTests() noexcept {
  HCoreLibTest::begin("HAdc");

  checkEmptyTable();
  checkDividerIsUndone();
  checkFullRangeSweep();
  checkFailedReadsAreNotZero();
  checkRawIsNotMillivolts();
  checkBadDividerIsRefused();

  checkCurveKnees();
  checkCurveInterpolates();
  checkCurveClamps();
  checkCurveIsMonotonic();
  checkCurveDegenerates();

  backend().reset();
}
