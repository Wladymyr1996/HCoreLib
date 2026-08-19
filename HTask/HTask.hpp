#pragma once

#include <cstdint>

#include <HTaskManager/HIWatchable/HIWatchable.hpp>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief One FreeRTOS task, as an object.
 *
 * A task in this ecosystem is a class deriving from this one: it owns its own
 * state, its own loop, and nothing else's. The base holds the part that would
 * otherwise be copied into every one of them - the handle, the sizing, and the
 * C-callable trampoline FreeRTOS needs because it cannot call a member function.
 *
 * A derived class implements run() and is started once:
 *
 * @code
 *   static Aht20Task sensor(bus);
 *   sensor.start();
 * @endcode
 *
 * run() is not expected to return. If it does, the task deletes itself rather
 * than falling off the end of a FreeRTOS task, which would abort the firmware.
 *
 * The object must outlive the task, so give it static storage duration - the
 * trampoline dereferences `this` on every wake-up.
 *
 * It is an HIWatchable because that is all HTaskManager needs of it - a name -
 * and implementing the watchdog's own interface is what lets the watchdog stay
 * ignorant of this class. Nothing about starting or running a task goes
 * through it.
 */
class HTask : public HIWatchable {
 public:
  /** @brief Passed as the timeout to leave a task unwatched. */
  static constexpr uint32_t kNoWatchdog = 0;

  /**
   * @param name Shown by the debugger and by HLog on every line this task logs.
   * @param stackBytes Stack size. ESP-IDF counts BYTES here, not words.
   * @param priority FreeRTOS priority; higher numbers pre-empt lower ones.
   * @param watchdogMs How long this task may go without reporting before
   *        HTaskManager restarts the device. Default: not watched.
   *
   *        Give it a value comfortably longer than one loop, and remember what
   *        the loop actually waits for - a sensor task that blocks 80 ms for a
   *        conversion and then sleeps five seconds needs more than five seconds,
   *        not more than eighty milliseconds. A task that legitimately blocks
   *        with no upper bound - one parked in recvfrom() - should stay
   *        kNoWatchdog rather than be given a generous number.
   */
  HTask(const char* name, uint32_t stackBytes, uint32_t priority,
        uint32_t watchdogMs = kNoWatchdog) noexcept;

  virtual ~HTask();

  // Non-copyable: the trampoline holds a pointer to one specific instance.
  HTask(const HTask&) = delete;
  HTask& operator=(const HTask&) = delete;

  /**
   * @brief Creates the FreeRTOS task. Idempotent - a second call does nothing.
   * @return false if the task could not be created, which on this chip means
   *         there was not enough free heap for its stack.
   */
  bool start() noexcept;

  /** @brief True once the task exists. */
  bool isRunning() const noexcept;

  /** @brief The task's name, as given to the constructor. */
  const char* name() const noexcept override;

  /** @brief The watchdog timeout this task was built with. */
  uint32_t watchdogMs() const noexcept;

 protected:
  /** @brief The task body. Runs forever; see the class note if it returns. */
  virtual void run() = 0;

  /**
   * @brief Reports the task alive, then sleeps for `ms`.
   *
   * The watchdog is fed HERE rather than by anything a derived class has to
   * remember: every loop in this ecosystem ends by sleeping, so a task that got
   * as far as its own sleep call is a task that completed an iteration. Use this
   * instead of HSystemUtils::sleep() inside a task and the watchdog needs no
   * further thought.
   */
  void sleep(uint32_t ms) noexcept;

  /**
   * @brief Reports the task alive without sleeping.
   *
   * For a loop shaped differently - one that blocks on a queue or a socket with
   * a timeout of its own, and so has no sleep to hang this on.
   */
  void alive() noexcept;

 private:
  /** @brief FreeRTOS entry point: turns the void* back into an object. */
  static void trampoline(void* argument);

  const char* name_;
  uint32_t stackBytes_;
  uint32_t priority_;
  uint32_t watchdogMs_;
  TaskHandle_t handle_;
};
