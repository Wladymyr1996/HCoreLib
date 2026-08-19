#include "HCoreLibTest.hpp"

#include <cstring>

#include <HGpioManager/HGpioDesktop/HGpioDesktop.hpp>
#include <HGpioManager/HGpioManager.hpp>

/**
 * @file HGpioTest.cpp
 * @brief The board table, and the one translation that happens above it.
 *
 * Inversion is the whole subject. `invert: true` in Tests/Config/HGpioConfig.h
 * says the pad's LOW means the logical TRUE, and HGpioPin is the last place
 * that knows it - everything above reads "pressed" and "demanded". So every
 * check here drives the PAD through the desktop backend and asserts on the
 * MEANING, which is the direction a real wire works in.
 */

namespace {

HGpioDesktop& gpio() {
  return static_cast<HGpioDesktop&>(HGpioManager::instance());
}

void checkTable() {
  CHECK(HGpioManager::configureAll());

  // Three fixed rows plus one configurable, presented as one flat table.
  CHECK(HGpioManager::pinCount() == 4);

  const HGpioPin button = HGpioManager::find("btn");
  REQUIRE(button.isValid());
  CHECK(std::strcmp(button.name(), "btn") == 0);
  CHECK(button.number() == 2);
  CHECK(!button.isOutput());
  CHECK(button.isActiveLow());

  const HGpioPin led = HGpioManager::find("led");
  REQUIRE(led.isValid());
  CHECK(led.isOutput());
  CHECK(!led.isActiveLow());

  CHECK(HGpioManager::find("sensor").isValid());  // the configurable row

  // at() walks the same table find() searches.
  CHECK(HGpioManager::at(0).isValid());
  CHECK(std::strcmp(HGpioManager::at(0).name(), "btn") == 0);
  CHECK(!HGpioManager::at(HGpioManager::pinCount()).isValid());

  // Idempotent, because a mode that reconfigures the world after a soft reset
  // calls it again.
  CHECK(HGpioManager::configureAll());
}

/** @brief A name nobody declared degrades to a pin that does nothing. */
void checkUnknownName() {
  const HGpioPin missing = HGpioManager::find("nosuchpin");

  CHECK(!missing.isValid());
  CHECK(!missing.read());
  CHECK(std::strcmp(missing.name(), "") == 0);
  CHECK(missing.number() == -1);

  missing.write(true);  // a no-op, not a fault
  CHECK(!missing.read());

  CHECK(!HGpioManager::find(nullptr).isValid());
}

void checkInputInversion() {
  const HGpioPin button = HGpioManager::find("btn");
  REQUIRE(button.isValid());

  // The pull-up holds the pad HIGH while the switch is open, and the row says
  // that means "not pressed".
  gpio().setRawLevel(button.number(), true);
  CHECK(!button.read());

  gpio().setRawLevel(button.number(), false);
  CHECK(button.read());

  // The plain input has no inversion to apply.
  const HGpioPin sensor = HGpioManager::find("sensor");
  REQUIRE(sensor.isValid());
  gpio().setRawLevel(sensor.number(), true);
  CHECK(sensor.read());
  gpio().setRawLevel(sensor.number(), false);
  CHECK(!sensor.read());
}

void checkOutputInversion() {
  const HGpioPin led = HGpioManager::find("led");
  const HGpioPin relay = HGpioManager::find("relay");
  REQUIRE(led.isValid());
  REQUIRE(relay.isValid());

  led.write(true);
  CHECK(gpio().readRaw(led.number()));
  CHECK(led.read());
  led.write(false);
  CHECK(!gpio().readRaw(led.number()));

  // An inverted output drives the pad the other way, and reads back as what it
  // MEANS rather than as what the pad is doing.
  relay.write(true);
  CHECK(!gpio().readRaw(relay.number()));
  CHECK(relay.read());
  relay.write(false);
  CHECK(gpio().readRaw(relay.number()));
  CHECK(!relay.read());
}

/** @brief An input that could be driven is a short circuit waiting to happen. */
void checkInputsCannotBeDriven() {
  const HGpioPin button = HGpioManager::find("btn");
  REQUIRE(button.isValid());

  gpio().setRawLevel(button.number(), true);
  button.write(true);
  CHECK(gpio().readRaw(button.number()));  // unchanged: the write did nothing
}

}  // namespace

void runGpioTests() noexcept {
  HCoreLibTest::begin("HGpioManager");

  checkTable();
  checkUnknownName();
  checkInputInversion();
  checkOutputInversion();
  checkInputsCannotBeDriven();
}
