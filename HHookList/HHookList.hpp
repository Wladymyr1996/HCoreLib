#pragma once

#include <cstddef>

#include <etl/delegate.h>
#include <etl/vector.h>

/** @brief What every hook looks like: takes nothing, returns nothing. */
using HHook = etl::delegate<void()>;

/**
 * @brief A fixed-capacity list of callbacks a library module invites the
 *        application into.
 *
 * The shape every "tell me when this happens" in HCoreLib uses, so an
 * application learns it once. A library module owns a list, an application adds
 * to it, and the module calls them at the moment only it knows about:
 *
 * @code
 *   HSleep::onBeforeSleep().add(HHook::create<&parkThePanel>());
 * @endcode
 *
 * ## Why hooks instead of the module just doing the work
 * HSleep cannot park a display: it does not know this device has one. Nor
 * should it - the next device in the ecosystem has a relay to open instead. A
 * hook is how the library keeps the ORDER (which only it knows) while the
 * application keeps the ACTIONS (which only it knows).
 *
 * ## Rules
 * Hooks run in whatever context fired them and in the order they were added.
 * They must be short and must not block - most of the moments worth hooking are
 * moments the device is about to stop.
 *
 * No heap: capacity is fixed at compile time, and a list that is full says so
 * rather than growing.
 */
template <size_t CAPACITY>
class HHookList {
 public:
  /**
   * @brief Adds a callback.
   * @return false if the list is full, in which case nothing was added.
   */
  bool add(const HHook& hook) {
    if (hooks_.full() || !hook.is_valid()) {
      return false;
    }

    hooks_.push_back(hook);
    return true;
  }

  /** @brief Calls every hook, in the order they were added. */
  void invoke() const {
    for (const HHook& hook : hooks_) {
      hook();
    }
  }

  /** @brief How many callbacks are registered. */
  size_t size() const {
    return hooks_.size();
  }

  /** @brief Forgets every callback. */
  void clear() {
    hooks_.clear();
  }

 private:
  etl::vector<HHook, CAPACITY> hooks_;
};
