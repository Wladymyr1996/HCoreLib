#include "HTask.hpp"

#define HLOG_MODULE_NAME "Task"
#include <HLog/HLog.hpp>

#include <HSystemUtils/HSystemUtils.hpp>
#include <HTaskManager/HTaskManager.hpp>

HTask::HTask(const char* name, uint32_t stackBytes, uint32_t priority,
             uint32_t watchdogMs) noexcept
    : name_(name),
      stackBytes_(stackBytes),
      priority_(priority),
      watchdogMs_(watchdogMs),
      handle_(nullptr) {
}

// Out-of-line destructor: anchors the vtable in this translation unit instead
// of emitting it in every file that derives from this class.
HTask::~HTask() = default;

void HTask::trampoline(void* argument) {
  HTask* task = static_cast<HTask*>(argument);
  task->run();

  // run() is not supposed to return, but a FreeRTOS task that falls off the end
  // of its function aborts the firmware. Deleting itself turns a design mistake
  // into one dead task instead of a panic.
  //
  // Unenrolling FIRST is what stops that dead task from being read as a hung
  // one: a sensor task that gave up because its device is not fitted would
  // otherwise stop reporting and have the watchdog restart the device, over and
  // over, for a part that was never there.
  HTaskManager::instance().remove(*task);

  HCritical("[%s] run() returned - deleting the task", task->name());
  task->handle_ = nullptr;
  vTaskDelete(nullptr);
}

bool HTask::start() noexcept {
  if (handle_ != nullptr) {
    return true;
  }

  // Enrolled BEFORE the task exists, so its first iteration is already covered
  // and no task can be watched and forget to register.
  HTaskManager::instance().add(*this, watchdogMs_);

  const BaseType_t result =
      xTaskCreate(&HTask::trampoline, name_, stackBytes_, this, priority_, &handle_);

  if (result != pdPASS) {
    HCritical("[%s] could not be created - not enough heap for %u bytes of stack", name_,
              static_cast<unsigned>(stackBytes_));
    HTaskManager::instance().remove(*this);
    handle_ = nullptr;
    return false;
  }

  return true;
}

bool HTask::isRunning() const noexcept {
  return handle_ != nullptr;
}

const char* HTask::name() const noexcept {
  return name_;
}

uint32_t HTask::watchdogMs() const noexcept {
  return watchdogMs_;
}

void HTask::alive() noexcept {
  if (watchdogMs_ != kNoWatchdog) {
    HTaskManager::instance().alive(*this);
  }
}

void HTask::sleep(uint32_t ms) noexcept {
  alive();
  HSystemUtils::sleep(ms);
}
