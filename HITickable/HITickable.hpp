#pragma once

#include <HCoreLib.h>

/**
 * @brief Something that has to be given the CPU regularly to do its job.
 *
 * HCoreLib has no scheduler and starts no tasks of its own. Anything that must
 * notice things over time - a button settling, a sensor finishing a conversion,
 * a timeout running out - implements this interface, and the application calls
 * update() on it every HCORELIB_TICK_MS from a task it owns:
 *
 * @code
 *   void tickTask(void* arg) {
 *     TickType_t last = xTaskGetTickCount();
 *     for (;;) {
 *       for (HITickable* item : tickables) {
 *         item->update();
 *       }
 *       vTaskDelayUntil(&last, pdMS_TO_TICKS(HCORELIB_TICK_MS));
 *     }
 *   }
 * @endcode
 *
 * update() takes no elapsed time on purpose. Durations are measured with HTimer
 * against an absolute clock, so an implementation never needs to be told how
 * long the last tick took - which means a late tick delays when something is
 * NOTICED without corrupting how long it TOOK.
 *
 * The cadence is a contract in one direction only: implementations may be
 * called more often than every HCORELIB_TICK_MS and must stay correct, but they may
 * assume they are not called much less often. update() runs in the caller's
 * task, so it must not block.
 */
class HITickable {
 public:
  virtual ~HITickable();

  /** @brief One slice of work. Called about every HCORELIB_TICK_MS; must not block. */
  virtual void update() noexcept = 0;
};
