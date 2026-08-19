#pragma once

/**
 * @brief Something HTaskManager can watch. All it needs is a name.
 *
 * The watchdog compares timestamps and, when one of them goes stale, has to say
 * WHICH task stopped reporting - and that is the whole of what it needs from
 * the thing it is watching. Asking for an HTask instead made the manager depend
 * on the class that already depends on IT, for one call to name().
 *
 * So the manager declares what it can watch, HTask implements it, and the
 * dependency runs one way. Nothing else changes: a task still enrols itself
 * from start(), because a task that can be watched must not be able to forget
 * to register.
 *
 * The pointer is also the IDENTITY - add(), alive() and remove() find an entry
 * by address - so an implementation must outlive its registration, which for an
 * HTask it already must: the FreeRTOS trampoline dereferences it on every
 * wake-up.
 */
class HIWatchable {
 public:
  virtual ~HIWatchable();

  /** @brief The name the watchdog reports if this one stops reporting. */
  virtual const char* name() const noexcept = 0;
};
