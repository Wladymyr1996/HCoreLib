#pragma once

#include <cstdint>

#include <HCoreLib.h>

/**
 * @file HStatusLed.hpp
 * @brief The family's status LED: what a device is doing, in one RGB LED.
 *
 * ## Opt-in
 * HSTATUSLED_ENABLE is 0 unless the application's HCoreLibConfig.h says 1. At 0
 * every call below is an inline no-op and nothing is linked - no RMT, no
 * pattern table - so an application calls it unconditionally, on a board with
 * the LED or without, and writes no `#if` of its own.
 */

/** 1 to drive the on-board status LED. Off by default: not every board has one. */
#ifndef HSTATUSLED_ENABLE
#define HSTATUSLED_ENABLE 0
#endif

/**
 * How bright the LED is at full, out of 255. Every colour is scaled by this.
 *
 * Low on purpose. A bare WS2812 at full current lights a room at night, and a
 * status LED is something glanced at, not read by. About 1/16.
 */
#ifndef HSTATUSLED_BRIGHTNESS
#define HSTATUSLED_BRIGHTNESS 16
#endif

/** Byte orders for HSTATUSLED_COLOR_ORDER. */
#define HSTATUSLED_ORDER_GRB 0
#define HSTATUSLED_ORDER_RGB 1

/**
 * The order the part wants its three bytes in. WS2812 and most clones are
 * G, R, B. If green shows as red on a new board, this is the switch.
 */
#ifndef HSTATUSLED_COLOR_ORDER
#define HSTATUSLED_COLOR_ORDER HSTATUSLED_ORDER_GRB
#endif

/**
 * @brief What the device IS right now: its boot mode. Always shown, unless a
 *        HStatusLedOverlay is set.
 */
enum class HStatusLedBase : uint8_t {
  None,          ///< Dark.
  Normal,        ///< Green, 50 ms every 2 s.
  Configuring,   ///< Yellow, 50 ms every 2 s.
  FactoryReset,  ///< Yellow, 50 ms on / 50 ms off.
  Failed,        ///< Red, 1 s on / 0.5 s off. Something the device needs did not start.

  /**
   * Red, 50 ms every 2 s: Normal mode's heartbeat, in red. The device runs, but
   * something it runs is not as configured - a relay controller with a relay
   * on its failsafe because the logic driving it went silent. Appended, so no
   * existing value moves.
   */
  Degraded,
};

/**
 * @brief Something the device is DOING for a while, shown instead of the base.
 */
enum class HStatusLedOverlay : uint8_t {
  None,        ///< Nothing: the base shows.
  BindSlave,   ///< Looking for a master. Yellow, 200 ms on / 200 ms off.
  BindMaster,  ///< The window for a child is open. Solid yellow.
};

/** @brief A colour at full scale, before HSTATUSLED_BRIGHTNESS. */
struct HStatusLedColor {
  uint8_t red;
  uint8_t green;
  uint8_t blue;

  bool operator==(const HStatusLedColor& other) const noexcept {
    return red == other.red && green == other.green && blue == other.blue;
  }
  bool operator!=(const HStatusLedColor& other) const noexcept { return !(*this == other); }
};

/**
 * @brief One status's look: a colour, lit for onMs, dark for offMs, repeating.
 *
 * offMs 0 is solid. onMs 0 is dark. Every cycle starts LIT, so a new status is
 * visible the moment it is set rather than up to offMs later.
 */
struct HStatusLedPattern {
  HStatusLedColor color;
  uint16_t onMs;
  uint16_t offMs;
};

#if HSTATUSLED_ENABLE

class HIRgbLed;

/**
 * @brief The status LED. One per board, so one static class, like HGpioManager.
 *
 * ## The standard patterns
 * | Status | Colour | Pattern |
 * | ------ | ------ | ------- |
 * | Bind: looking for a master | yellow | 200 ms on / 200 ms off |
 * | Bind: master window open | yellow | solid |
 * | Normal mode | green | 50 ms on / 2 s off |
 * | Settings mode | yellow | 50 ms on / 2 s off |
 * | Factory reset | yellow | 50 ms on / 50 ms off |
 * | Failed | red | 1 s on / 0.5 s off |
 *
 * The same on every device in the family, and defined here, once, so that no
 * device can quietly mean something else by a yellow blink.
 *
 * ## Base and overlay
 * A device has one BASE status - its boot mode - and at most one OVERLAY - a
 * bind in progress. An overlay replaces the base while it is set; clearing it
 * brings the base back. Setting the status it already has changes nothing and
 * does not restart the pattern.
 *
 * ## Ticked by the task that owns it
 * update() is called every HCORELIB_TICK_MS by the device's main loop, and not
 * from a timer of its own - so the Normal-mode blip doubles as a sign of life:
 * a wedged loop stops blinking rather than looking healthy. The one mode with
 * no loop, a factory reset's blocking erase, uses runFor().
 *
 * ## One thread
 * Everything here is called from one task - app_main before the tasks start,
 * then the owning task. There is no lock.
 */
class HStatusLed {
 public:
  HStatusLed() = delete;

  /**
   * @brief Claims the LED and shows dark. Call once, at start-up.
   * @return false when the LED could not be claimed; every other call is then
   *         harmless and shows nothing.
   */
  static bool begin() noexcept;

  static void setBase(HStatusLedBase base) noexcept;
  static void setOverlay(HStatusLedOverlay overlay) noexcept;
  static void clearOverlay() noexcept;

  static HStatusLedBase base() noexcept;
  static HStatusLedOverlay overlay() noexcept;

  /** @brief Advances the pattern. Call every HCORELIB_TICK_MS. */
  static void update() noexcept;

  /**
   * @brief update() with the time given. What update() calls, and what a test
   *        drives so that a pattern can be checked to the millisecond.
   */
  static void tick(uint32_t nowMs) noexcept;

  /**
   * @brief Blocks for @p durationMs, ticking the LED.
   *
   * For a mode with no loop of its own - a factory reset - so its pattern is
   * seen before the device restarts.
   */
  static void runFor(uint32_t durationMs) noexcept;

  // -- the table, for tests and for anything documenting it ----------------

  static const HStatusLedPattern& pattern(HStatusLedBase base) noexcept;
  static const HStatusLedPattern& pattern(HStatusLedOverlay overlay) noexcept;

  /** @brief Whether @p pattern is lit @p elapsedMs after it started. */
  static bool isLit(const HStatusLedPattern& pattern, uint32_t elapsedMs) noexcept;

  /** @brief @p color at HSTATUSLED_BRIGHTNESS. A non-zero channel stays non-zero. */
  static HStatusLedColor scale(const HStatusLedColor& color) noexcept;

  /** @brief The backend, so a host test can reach HRgbLedDesktop. */
  static HIRgbLed& driver() noexcept;
};

#else  // HSTATUSLED_ENABLE

/**
 * @brief HSTATUSLED_ENABLE is 0: the same calls, doing nothing.
 *
 * Declared rather than #if'd out at every call site, so an application is
 * written once and builds for a board with the LED and one without.
 */
class HStatusLed {
 public:
  HStatusLed() = delete;

  static bool begin() noexcept { return false; }
  static void setBase(HStatusLedBase) noexcept {}
  static void setOverlay(HStatusLedOverlay) noexcept {}
  static void clearOverlay() noexcept {}
  static HStatusLedBase base() noexcept { return HStatusLedBase::None; }
  static HStatusLedOverlay overlay() noexcept { return HStatusLedOverlay::None; }
  static void update() noexcept {}
  static void tick(uint32_t) noexcept {}
  static void runFor(uint32_t) noexcept {}
};

#endif  // HSTATUSLED_ENABLE
