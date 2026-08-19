#pragma once

#include <HHookList/HHookList.hpp>
#include <HCoreLib.h>

/** How many callbacks the erase hook will hold. */
#ifndef HFACTORYRESET_MAX_HOOKS
#define HFACTORYRESET_MAX_HOOKS 4
#endif

/**
 * @brief Erasing everything the device was told, and coming back as new.
 *
 * Two halves, because a factory reset cannot happen where it is asked for.
 *
 * ## Asking
 * request() is called by the REST handler AFTER its response has been sent. It
 * only sets a flag: rebooting inside an HTTP handler would drop the socket
 * before the client had read the answer, and the client would report a failure
 * for something that succeeded.
 *
 * ## Doing
 * MainTask notices the flag and reboots into HBootModeKind::FactoryReset, and
 * that boot calls perform(): the erase happens in a firmware with no radio, no
 * server and nothing else running, so there is nothing to write a file back
 * behind it. Then it reboots into Normal.
 *
 * ## What it clears
 * Every file under `config/` - settings, and the admin password with them - plus
 * the RTC mirror that would otherwise put the old language and units straight
 * back. What survives is what a reset should not touch: the OTA slots, the
 * Wi-Fi calibration data in NVS, and the readings themselves, which are
 * observations rather than configuration.
 */
class HFactoryReset {
 public:
  HFactoryReset() = delete;

  /**
   * @brief Marks the device for a reset on the next chance. Does NOT reboot.
   *
   * Safe from any task; the flag is read by MainTask.
   */
  static void request() noexcept;

  /** @brief True once request() has been called and nothing has acted on it yet. */
  static bool isRequested() noexcept;

  /**
   * @brief Called after `config/` is erased and before the device restarts.
   *
   * Where an application throws away whatever it caches OUTSIDE those files -
   * an RTC mirror of the settings, most obviously. Deleting a config file while
   * a copy of it survives in retained memory would hand the old values straight
   * back on the next boot, and the reset would look like it had not happened.
   *
   * This class cannot know what those caches are, which is exactly why it asks.
   */
  static HHookList<HFACTORYRESET_MAX_HOOKS>& onErase() noexcept;

  /**
   * @brief Erases the configuration and restarts into Normal. NEVER RETURNS.
   *
   * Call only from the FactoryReset boot mode, with the filesystem mounted.
   */
  [[noreturn]] static void perform() noexcept;
};
