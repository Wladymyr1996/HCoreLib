#pragma once

#include <cstddef>
#include <cstdint>

#include <HCoreLib.h>

/** How many retained blocks the registry holds. */
#ifndef HRTCSTORE_MAX_BLOCKS
#define HRTCSTORE_MAX_BLOCKS 6
#endif

/**
 * @brief The bookkeeping every block of retained memory needs.
 *
 * RTC memory survives a reset and deep sleep but is **uninitialised** after a
 * power-on - not zeroed, not cleared, just whatever the SRAM powered up
 * holding. So a block cannot be read until it has been proved, and every module
 * that keeps something there ends up writing the same magic-word dance.
 *
 * This is that dance, once, plus a registry that makes it possible to throw the
 * lot away:
 *
 * @code
 *   RTC_DATA_ATTR MyBlock block;      // no constructor - see below
 *
 *   void MyModule::init() {
 *     HRtcStore::track(block.magic);
 *     if (!HRtcStore::isValid(block.magic, kMyMagic)) { seedDefaults(); }
 *   }
 * @endcode
 *
 * ## Why a registry
 * HTaskManager restarts a hung device "cold" - as if power had been removed -
 * which means the next boot must not find stale state vouching for itself.
 * invalidateAll() zeroes every tracked magic, so a block that uses this class is
 * covered automatically rather than by each module remembering to register a
 * hook it can forget.
 *
 * ## The one rule for a retained block
 * It must be plain data. A struct with a constructor gets that constructor run
 * at every boot, which writes over the very bytes that were supposed to
 * survive - the retention is real, the initialisation destroys it.
 */
class HRtcStore {
 public:
  HRtcStore() = delete;

  /**
   * @brief Registers a block's magic word so invalidateAll() can reach it.
   *
   * Call once, from the owning module's init(). Registering the same word twice
   * is harmless and does not consume a second slot.
   */
  static void track(uint32_t& magic) noexcept;

  /**
   * @brief True if this block was written by this firmware and survived.
   * @param magic The block's stored word.
   * @param expected The value this module writes. Give each block its own -
   *        two blocks sharing a magic would each vouch for the other's garbage.
   */
  static bool isValid(uint32_t magic, uint32_t expected) noexcept;

  /**
   * @brief Zeroes every tracked magic word: the next boot looks like a power-on.
   *
   * Contents are left alone; only the proof goes. Nothing in flash is touched -
   * a fault must never cost the owner their configuration.
   */
  static void invalidateAll() noexcept;

  /** @brief How many blocks are currently tracked. For diagnostics. */
  static size_t trackedCount() noexcept;
};
