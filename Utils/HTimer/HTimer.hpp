#pragma once

#include <cstdint>

/**
 * @brief A one-shot software timeout, polled rather than fired.
 *
 * The unit of time for everything in HCoreLib that has to notice things over
 * time. There is no callback, no task and no interrupt here: a timer is three
 * words of state that answer one question - "has my timeout passed?" - and code
 * built around update() (see HITickable) asks it once a tick.
 *
 * ## Why an absolute clock rather than an accumulator
 * Every duration is measured against HSystemUtils::millis(), captured when the
 * timer was armed. It is NOT accumulated from the caller's tick period, and
 * that is the whole point: a task that runs late, or misses a tick entirely
 * because something above it took too long, still reads the correct elapsed
 * time. An accumulator would quietly under-count instead, and a debounce that
 * under-counts is a button that double-fires.
 *
 * ## Wrap-safe by construction
 * millis() rolls over every 49.7 days. Comparing `now - startedAt >= timeout`
 * on unsigned arithmetic stays correct across that rollover; the
 * natural-looking `now >= startedAt + timeout` does not. This class is the only
 * place in HCoreLib that has to get that right.
 *
 * @code
 *   HTimer debounce(30);
 *   debounce.start();
 *   ...
 *   if (debounce.isExpired()) { ... }   // stays true until reset()/stop()
 * @endcode
 *
 * Task context only on the MCU (see HSystemUtils::millis()). Copyable, no heap,
 * no virtuals - cheap enough to have several per object.
 */
class HTimer {
 public:
  /**
   * @brief Creates a stopped timer.
   * @param timeoutMs How long start() should run for. May be set later.
   */
  explicit HTimer(uint32_t timeoutMs = 0) noexcept;

  /** @brief Arms the timer, counting from now, with the timeout it already has. */
  void start() noexcept;

  /**
   * @brief Sets the timeout and arms the timer, counting from now.
   * @param timeoutMs New timeout, kept for later start()/reset() calls.
   */
  void start(uint32_t timeoutMs) noexcept;

  /**
   * @brief Disarms the timer. isExpired() is false while stopped.
   *
   * elapsedMs() freezes at the value it had, so a caller can still ask how long
   * a stopped timer ran for - which is how HButton reports a press duration.
   */
  void stop() noexcept;

  /** @brief Arms the timer again from now, keeping the same timeout. */
  void reset() noexcept;

  /** @brief True once `timeoutMs` has passed since the timer was armed. Stays true until reset(). */
  bool isExpired() const noexcept;

  /** @brief True between start() and stop(), expired or not. */
  bool isRunning() const noexcept;

  /** @brief Milliseconds since the timer was armed; frozen while stopped, 0 if never started. */
  uint32_t elapsedMs() const noexcept;

  /** @brief Milliseconds left before expiry; 0 once expired or while stopped. */
  uint32_t remainingMs() const noexcept;

  /** @brief The configured timeout, whether running or not. */
  uint32_t timeoutMs() const noexcept;

 private:
  uint32_t timeoutMs_;
  uint32_t startedAtMs_;
  uint32_t frozenElapsedMs_;  ///< What elapsedMs() reports once stopped.
  bool running_;
};
