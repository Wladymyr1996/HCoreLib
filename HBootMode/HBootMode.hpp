#pragma once

#include <cstdint>

#include <HCoreLib.h>

/**
 * @file HBootMode.hpp
 * @brief Which of the four isolated firmwares this boot is running.
 *
 * The enum below shares this header with the class: it is a plain enum with no
 * behaviour, so there is nothing for a .cpp of its own to hold and no vtable to
 * anchor.
 */

/** @brief How long rebootTo() lets the console UART drain before resetting, in ms. */
#ifndef HBOOTMODE_UART_DRAIN_MS
#define HBOOTMODE_UART_DRAIN_MS 50
#endif

/**
 * @brief What the node does after app_main() decides.
 *
 * These are not flags and not a state machine - they are DISJOINT firmwares
 * sharing one binary. Exactly one runs per boot, and switching means rebooting.
 * That isolation is the point: a node in Normal has no Wi-Fi stack in its
 * working set, so it cannot be attacked over Wi-Fi, cannot spend RAM on an HTTP
 * server, and cannot have its work interrupted by a beacon interval.
 */
enum class HBootModeKind : uint8_t {
  /**
   * Standard operation: the device does its actual job. No Wi-Fi AP or STA, no
   * web server. The mode a device spends essentially all of its life in.
   */
  Normal = 0,

  /**
   * Setup. Comes up as a Wi-Fi Access Point serving the configuration UI and
   * accepting a manual firmware upload. Deliberately does no control work: a
   * node being configured is not a node controlling anything.
   */
  Configuring = 1,

  /**
   * Fleet update. Comes up as a Wi-Fi Station, downloads a .bin into the
   * inactive OTA slot, marks it bootable and resets. Does one job and reboots,
   * in either direction.
   */
  Ota = 2,

  /**
   * Factory reset. Erases the device's configuration and reboots into Normal, so
   * the node comes back up on the defaults compiled into it. No radio, no UI -
   * it is the shortest mode there is.
   *
   * Meant to be self-clearing: whatever it wipes must include the instruction
   * that sent it here, so the reset happens exactly once and cannot loop.
   */
  FactoryReset = 3
};

/**
 * @brief Decides, records and reports the boot mode. A static facade.
 *
 * ## Asking for a mode
 * A request has to survive a reset, and RTC memory is where this chip keeps
 * something across one for free - no flash erase cycle, so it is safe to call
 * from a command handler or a button ISR's task:
 *
 * @code
 *   // in app_main, before anything else decides what to do
 *   switch (HBootMode::current()) {
 *     case HBootModeKind::Normal: ... break;
 *     case HBootModeKind::Configuring: ... break;
 *     ...
 *   }
 *
 *   // from a command handler: "restart me into setup"
 *   HBootMode::rebootTo(HBootModeKind::Configuring);
 * @endcode
 *
 * The request is a ONE-SHOT: it survives a software reset and deep sleep, but
 * not a power cut, and it is consumed as it is read. A device that entered
 * Configuring by button press therefore returns to Normal after a power cycle,
 * which is the behaviour you want when a node is on a pole - and a firmware
 * that crashes in Configuring comes back up in Normal rather than looping.
 *
 * ## The guard word is what makes RTC readable at all
 * RTC memory is uninitialised after a cold boot, not zeroed. Reading a mode out
 * of it without a check would give a factory-fresh device whatever the SRAM
 * happened to power up holding - a one-in-four chance of booting an OTA client
 * that has nowhere to connect to. A magic word plus a range check is the whole
 * of the validation, and a failure is silent and normal rather than an error: it
 * just means this was a cold boot.
 *
 * ## What is not here yet
 * A STICKY default ("this node is a configuration kiosk until somebody says
 * otherwise") needs somewhere in flash to live, which is HConfig's job. Until
 * that module exists, a boot with no pending request is Normal.
 */
class HBootMode {
 public:
  HBootMode() = delete;

  /**
   * @brief Resolves the boot mode ONCE and caches it. Call first in app_main().
   *
   * Consumes any pending one-shot request as it reads it, so the same request
   * cannot decide two boots in a row.
   * @return The mode this boot runs in; Normal when nothing was requested.
   */
  static HBootModeKind resolve() noexcept;

  /**
   * @brief The resolved mode, resolving on first use.
   *
   * Cheap enough to call anywhere after the first: one cached byte.
   */
  static HBootModeKind current() noexcept;

  /**
   * @brief Asks for `mode` on the NEXT boot only. Does NOT reboot.
   *
   * Writes RTC memory and nothing else - no flash, no erase cycle. Use this when
   * something else has to happen before the reset; otherwise use rebootTo(),
   * which is this call plus the restart.
   */
  static void requestOnce(HBootModeKind mode) noexcept;

  /**
   * @brief Records the request and restarts the chip into `mode`. Never returns.
   *
   * Waits HBOOTMODE_UART_DRAIN_MS for the console to finish shifting out first:
   * HLog writes straight to the UART with no buffer of its own, and the lines
   * explaining WHY a node rebooted are the ones worth not losing.
   *
   * It restarts immediately - it does not stop anything the application is
   * doing. Bring the device to a safe state before calling.
   */
  [[noreturn]] static void rebootTo(HBootModeKind mode) noexcept;

  /**
   * @brief Discards any pending one-shot request, so the next boot is Normal.
   *
   * For a restart that must behave like a power cut rather than like a reset -
   * see HTaskManager, which uses it so a device hung in Configuring does not
   * come back into Configuring and hang again.
   */
  static void clearRequest() noexcept;

  /** @brief "Normal", "Configuring", "Ota" or "FactoryReset". Never null. For logs and routes. */
  static const char* name(HBootModeKind mode) noexcept;

  /** @brief True if `raw` is one of the four defined modes. */
  static bool isValid(uint8_t raw) noexcept;

 private:
  /** @brief Reads and CONSUMES the one-shot request. @return false if there was none. */
  static bool takeRequest(HBootModeKind& out) noexcept;

  static HBootModeKind current_;
  static bool resolved_;
};
