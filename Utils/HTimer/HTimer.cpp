#include "HTimer.hpp"

#include <HSystemUtils/HSystemUtils.hpp>

HTimer::HTimer(uint32_t timeoutMs) noexcept
    : timeoutMs_(timeoutMs), startedAtMs_(0), frozenElapsedMs_(0), running_(false) {
}

void HTimer::start() noexcept {
  startedAtMs_ = HSystemUtils::millis();
  frozenElapsedMs_ = 0;
  running_ = true;
}

void HTimer::start(uint32_t timeoutMs) noexcept {
  timeoutMs_ = timeoutMs;
  start();
}

void HTimer::stop() noexcept {
  if (running_) {
    // Frozen before the flag drops, so a caller that stops a timer can still
    // ask how long it ran - HButton reports the press duration exactly this way.
    frozenElapsedMs_ = elapsedMs();
    running_ = false;
  }
}

void HTimer::reset() noexcept {
  start();
}

bool HTimer::isExpired() const noexcept {
  if (!running_) {
    return false;
  }
  return elapsedMs() >= timeoutMs_;
}

bool HTimer::isRunning() const noexcept {
  return running_;
}

uint32_t HTimer::elapsedMs() const noexcept {
  if (!running_) {
    return frozenElapsedMs_;
  }
  // Unsigned subtraction, which is what makes this correct across the 49.7-day
  // rollover of millis(): the difference is right even when `now` has wrapped
  // past `startedAtMs_`.
  return HSystemUtils::millis() - startedAtMs_;
}

uint32_t HTimer::remainingMs() const noexcept {
  if (!running_) {
    return 0;
  }
  const uint32_t elapsed = elapsedMs();
  return (elapsed >= timeoutMs_) ? 0U : (timeoutMs_ - elapsed);
}

uint32_t HTimer::timeoutMs() const noexcept {
  return timeoutMs_;
}
