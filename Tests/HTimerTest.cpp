#include "HCoreLibTest.hpp"

#include <HSystemUtils/HSystemUtils.hpp>
#include <HTimer/HTimer.hpp>

/**
 * @file HTimerTest.cpp
 * @brief The timer against a real clock, at durations short enough to be free.
 *
 * These sleep. There is no injectable clock in HTimer - it reads
 * HSystemUtils::millis() directly, which is the design (three words of state,
 * no indirection, cheap enough to have several per object) - so the honest way
 * to test it is to let a few tens of milliseconds actually pass.
 *
 * Every margin below is deliberately loose. A CI runner can and will lose the
 * scheduler for 30 ms at a time, and a test that fails when that happens is
 * worse than no test: it teaches everyone to ignore a red build.
 */

namespace {

/** @brief Short enough that the whole suite costs a fraction of a second. */
constexpr uint32_t kTimeoutMs = 30;

void checkStoppedTimer() {
  const HTimer timer(kTimeoutMs);

  CHECK(!timer.isRunning());
  CHECK(!timer.isExpired());       // a stopped timer never expires
  CHECK(timer.elapsedMs() == 0);
  CHECK(timer.remainingMs() == 0);
  CHECK(timer.timeoutMs() == kTimeoutMs);

  const HTimer defaulted;
  CHECK(defaulted.timeoutMs() == 0);
  CHECK(!defaulted.isRunning());
}

void checkExpiry() {
  HTimer timer(kTimeoutMs);
  timer.start();

  CHECK(timer.isRunning());
  CHECK(!timer.isExpired());
  CHECK(timer.remainingMs() <= kTimeoutMs);

  HSystemUtils::sleep(kTimeoutMs * 3);

  CHECK(timer.isExpired());
  CHECK(timer.isRunning());              // expired is not stopped
  CHECK(timer.remainingMs() == 0);
  CHECK(timer.elapsedMs() >= kTimeoutMs);

  // Stays true until somebody resets it - a poll that missed the moment must
  // still see it.
  HSystemUtils::sleep(5);
  CHECK(timer.isExpired());

  timer.reset();
  CHECK(!timer.isExpired());
  CHECK(timer.isRunning());
}

/** @brief stop() freezes elapsedMs(), which is how HButton reports a press. */
void checkStopFreezesElapsed() {
  HTimer timer(1000);
  timer.start();
  HSystemUtils::sleep(kTimeoutMs);
  timer.stop();

  const uint32_t frozen = timer.elapsedMs();
  CHECK(frozen >= kTimeoutMs);
  CHECK(!timer.isRunning());
  CHECK(!timer.isExpired());

  HSystemUtils::sleep(kTimeoutMs);
  CHECK(timer.elapsedMs() == frozen);

  // start(newTimeout) both re-arms and keeps the new value for later resets.
  timer.start(kTimeoutMs);
  CHECK(timer.timeoutMs() == kTimeoutMs);
  CHECK(timer.isRunning());
  CHECK(timer.elapsedMs() < kTimeoutMs);
}

/** @brief A zero timeout is already over the moment it is armed. */
void checkZeroTimeout() {
  HTimer timer(0);
  timer.start();

  CHECK(timer.isExpired());
  CHECK(timer.remainingMs() == 0);
}

}  // namespace

void runTimerTests() noexcept {
  HCoreLibTest::begin("HTimer");

  checkStoppedTimer();
  checkExpiry();
  checkStopFreezesElapsed();
  checkZeroTimeout();
}
