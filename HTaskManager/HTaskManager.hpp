#pragma once

#include <cstddef>
#include <cstdint>

#include <HCoreLib.h>

class HTask;

/** How many tasks the watchdog can watch at once. */
#ifndef HTASKMANAGER_MAX_TASKS
#define HTASKMANAGER_MAX_TASKS 8
#endif

/** How often the registry is checked, in ms. */
#ifndef HTASKMANAGER_CHECK_MS
#define HTASKMANAGER_CHECK_MS 500
#endif

/**
 * @brief Watches tasks for signs of life and restarts a device that loses one.
 *
 * ## Not the same thing as IDF's task watchdog
 * TWDT catches CPU STARVATION - the idle task never getting to run - and panics
 * with a stack trace. This catches a task that has stopped making PROGRESS while
 * the system as a whole looks healthy: one stuck in a blocked I2C read, or
 * looping on a flag nobody will set. Different failure, and the answer has to
 * name which task it was.
 *
 * ## Tasks enrol themselves
 * HTask::start() registers the task if it was built with a watchdog timeout, so
 * no task can be watched and forget to enrol. HTask::sleep() reports it alive on
 * the way past, which is why an ordinary loop needs no watchdog code at all;
 * HTask::alive() is there for a loop shaped differently.
 *
 * A task whose run() RETURNS is unenrolled, which matters more than it sounds:
 * a sensor task that gives up because its device is missing would otherwise stop
 * reporting, be declared hung, and put the device in a restart loop over a part
 * that is simply not fitted.
 *
 * ## What "restart" means here
 * Cold. Every retained block registered with HRtcStore is invalidated and the
 * pending boot-mode request is cleared, so the device comes back the way it
 * would after a power cut: Normal mode, no stale readings, settings re-read from
 * flash. Nothing in flash is erased - a fault must never cost the owner their
 * configuration.
 */
class HTaskManager {
 public:
  /** @brief The one instance. Tasks reach it from their own context. */
  static HTaskManager& instance() noexcept;

  /**
   * @brief Begins checking, every HTASKMANAGER_CHECK_MS.
   *
   * Call after the tasks are started. Checking runs from an esp_timer callback
   * rather than a task of its own: it compares timestamps and nothing else, so a
   * whole stack for it would be waste.
   */
  void start() noexcept;

  /**
   * @brief Stops checking without forgetting anything.
   *
   * For a deliberate long block - a firmware write, a flash erase - where a task
   * is doing exactly what it should and simply cannot report.
   */
  void suspend() noexcept;

  /** @brief Resumes checking, treating every task as having just reported. */
  void resume() noexcept;

  /**
   * @brief Registers a task. Called by HTask::start(); not by application code.
   * @param timeoutMs Silence permitted before the device is restarted.
   *        HTask::kNoWatchdog leaves the task unwatched.
   */
  void add(HTask& task, uint32_t timeoutMs) noexcept;

  /** @brief Forgets a task, for one whose run() has returned. */
  void remove(HTask& task) noexcept;

  /** @brief Records that a task is still working. Cheap enough for every loop. */
  void alive(const HTask& task) noexcept;

  /** @brief How many tasks are being watched. */
  size_t watchedCount() const noexcept;

 private:
  HTaskManager() = default;
};
