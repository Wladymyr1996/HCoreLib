#pragma once

#include <cstdint>

#include <HGpioManager/HGpioPin/HGpioPin.hpp>
#include <HHookList/HHookList.hpp>
#include <HCoreLib.h>

/** How many callbacks each of the sleep hooks below will hold. */
#ifndef HSLEEP_MAX_HOOKS
#define HSLEEP_MAX_HOOKS 4
#endif

/**
 * @brief What started this run of the firmware.
 *
 * Deep sleep is a RESET: app_main() runs again from the top on every wake, and
 * this is how it tells the three cases apart.
 */
enum class HWakeCause : uint8_t {
  /** Power-on, a reset button, a crash - anything that was not a wake. RAM is gone. */
  ColdBoot,

  /** The sleep timer ran out. Nobody is watching: do the periodic work and go back. */
  Timer,

  /** A pin the device armed before sleeping went active. Somebody IS watching. */
  Button,

  /** A wake source this class does not model. Treated as ColdBoot by callers. */
  Other,
};

/**
 * @brief Deep sleep: arming the wake sources, and the reason the last one ended.
 *
 * A static facade over the platform's sleep support, with no policy of its own -
 * how long to stay awake, and what to do with each wake cause, belongs to the
 * application.
 *
 * @code
 *   if (HSleep::wakeCause() == HWakeCause::Timer) { measureAndStore(); }
 *
 *   HSleep::enableTimerWakeup(60000);
 *   HSleep::enableButtonWakeup(buttonPin);
 *   HSleep::deepSleep();                 // never returns
 * @endcode
 *
 * ## What survives
 * Nothing in RAM. Statics are re-initialised, tasks are gone, peripherals are
 * reset. Only RTC memory persists - see HBootMode for the pattern, magic word
 * included - so anything the next run needs must live there.
 */
class HSleep {
 public:
  HSleep() = delete;

  /**
   * @brief Why this run started. Cheap, and safe to call more than once.
   */
  static HWakeCause wakeCause() noexcept;

  /**
   * @brief Called just before the wake sources are armed and the CPU stops.
   *
   * Where a device parks whatever it owns: a display turned off, an output
   * driven to its safe level, a last line flushed. This class cannot do any of
   * that itself - it does not know what this device has - but it does know the
   * one moment it must happen at, which is what a hook is for.
   *
   * Hooks run in the calling task, in the order they were added, and must not
   * block: everything after them is one function call from the chip stopping.
   */
  static HHookList<HSLEEP_MAX_HOOKS>& onBeforeSleep() noexcept;

  /**
   * @brief Called early on a boot that was a WAKE rather than a cold start.
   *
   * Invoked by notifyWake(), which the application calls once it has decided
   * this run is a wake - the library cannot pick that moment, because what has
   * to exist before a hook can run differs from device to device.
   */
  static HHookList<HSLEEP_MAX_HOOKS>& onAfterWake() noexcept;

  /**
   * @brief Runs the onAfterWake hooks, if this run began as a wake.
   *
   * Does nothing on a cold boot. Safe to call once per start-up.
   */
  static void notifyWake() noexcept;

  /**
   * @brief Wakes the chip after `ms` of sleep.
   * @param ms Sleep duration. The clock behind it runs in the RTC domain, so it
   *        keeps time while everything else is powered down.
   * @return false if the platform rejected the request.
   */
  static bool enableTimerWakeup(uint32_t ms) noexcept;

  /**
   * @brief Wakes the chip when `pin` goes ACTIVE, inversion included.
   *
   * The pin's logical "true" is what wakes the device, so a button wired to
   * ground behind a pull-up wakes on the press rather than on the release, and
   * this class needs to know nothing about the wiring to get that right.
   *
   * Only pads in the chip's low-power domain can do this (GPIO0-7 on the
   * ESP32-C6); anything else is rejected and logged. The pull resistor is
   * re-established through the RTC IO mux before sleeping, because the digital
   * pad control is powered down and the input would otherwise float - and a
   * floating wake pin wakes the device continuously.
   *
   * @return false if the pin is invalid or cannot wake the chip.
   */
  static bool enableButtonWakeup(const HGpioPin& pin) noexcept;

  /**
   * @brief Enters deep sleep. NEVER RETURNS - the chip resets on wake.
   *
   * Arm the wake sources first: with none armed, the device sleeps forever.
   * Everything that must outlive the nap has to be in RTC memory before this
   * call, and any peripheral that should be quiet (a display, a regulator)
   * must already have been told so.
   */
  [[noreturn]] static void deepSleep() noexcept;
};
