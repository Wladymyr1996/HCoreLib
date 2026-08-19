#pragma once

#include <cstddef>

#include <HCoreLib.h>

#include "HGpioPin/HGpioPin.hpp"
#include "HGpioTypes/HGpioTypes.hpp"

class HIGpio;

/**
 * @brief Owns the board's pins: configures them once, hands out handles by name.
 *
 * A static facade with no task, no interrupt and no state of its own beyond the
 * backend instance. Reading a pad is a register read, so there is nothing here
 * worth a thread: code that must notice a pin CHANGE polls it from update()
 * (see HITickable) and measures with HTimer. An edge interrupt would buy
 * nothing for a human-speed input and would force every consumer into
 * ISR-context rules - no logging, no flash, no blocking - for a signal that
 * bounces twenty times per press anyway.
 *
 * ## The board is declared, not discovered
 * Every pin comes from <HGpioConfig.h>, which the APPLICATION owns (it sits
 * beside HCoreLibConfig.h in HCORELIB_CONFIG_DIR) and which is the only file in
 * the firmware naming a GPIO number. The table's length is its capacity: a
 * board with one pin carries storage for one pin, and there is no
 * HGPIO_MAX_PINS to outgrow. Numbers, duplicates and reserved pads are checked
 * at COMPILE time, so a bad table fails the build rather than the boot.
 *
 * ## Everything above this class speaks names and meanings
 * find() takes "btn", never GPIO2, so moving the button to another pad is one
 * line in the board table. The HGpioPin it returns speaks logical levels, so
 * inversion is likewise invisible above this layer.
 *
 * @code
 *   HGpioManager::configureAll();               // once, early in app_main
 *   const HGpioPin button = HGpioManager::find("btn");
 *   if (button.read()) { ... }                  // true means PRESSED
 * @endcode
 */
class HGpioManager {
 public:
  HGpioManager() = delete;

  /**
   * @brief Configures every declared pin and parks the outputs. Call once, early.
   *
   * Idempotent - calling it again re-applies the same configuration, which is
   * what a mode that reconfigures the world after a soft reset wants.
   * @return false if any pin was rejected by the platform; the rest are still
   *         configured, and the failure is logged with the pin's name.
   */
  static bool configureAll() noexcept;

  /**
   * @brief The declared pin called `name`.
   * @param name As spelled in the board table; comparison is exact.
   * @return A handle, or an INVALID handle when no such pin is declared. Look
   *         a pin up once at start-up and keep the handle.
   */
  static HGpioPin find(const char* name) noexcept;

  /** @brief How many pins the board table declares. */
  static size_t pinCount() noexcept;

  /** @brief The pin at `index`, or an invalid handle. For diagnostics and tests. */
  static HGpioPin at(size_t index) noexcept;

  /**
   * @brief The compiled-in backend. HGpioPin reads and writes pads through it.
   *
   * Application code should not need this. Tests do: a host build can cast it
   * to HGpioDesktop and drive the pads that no wire is attached to.
   */
  static HIGpio& instance() noexcept;
};
