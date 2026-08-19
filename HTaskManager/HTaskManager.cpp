#include "HTaskManager.hpp"

#define HLOG_MODULE_NAME "TaskMgr"
#include <HLog/HLog.hpp>

#include <etl/mutex.h>
#include <etl/vector.h>

#include <esp_system.h>
#include <esp_timer.h>

#include <HBootMode/HBootMode.hpp>
#include <HRtcStore/HRtcStore.hpp>
#include <HSystemUtils/HSystemUtils.hpp>
#include <HTask/HTask.hpp>

namespace {

/** @brief One watched task: who, how long it may be silent, and when it last spoke. */
struct Entry {
  const HTask* task;
  uint32_t timeoutMs;
  uint32_t lastAliveMs;
};

using Registry = etl::vector<Entry, HTASKMANAGER_MAX_TASKS>;

Registry& registry() noexcept {
  static Registry entries;
  return entries;
}

/**
 * @brief Guards the registry.
 *
 * Written from every watched task and read from the timer callback, so it is
 * genuinely concurrent - and the entries are three words each, so a torn read
 * would compare one task's deadline against another's clock.
 */
etl::mutex& registryMutex() noexcept {
  static etl::mutex mutex;
  return mutex;
}

esp_timer_handle_t checkTimer = nullptr;
bool watching = false;

/**
 * @brief Restarts the device as though power had been removed.
 *
 * The invalidation is the point. A hung device that came back with its retained
 * state intact would resume whatever it was doing - including sitting in
 * Configuring, or trusting a cached reading taken before the fault. Clearing the
 * magic words makes the next boot look like a cold one to every module that
 * keeps something across a reset, while flash - the owner's actual settings - is
 * left untouched.
 */
[[noreturn]] void restartCold() noexcept {
  HRtcStore::invalidateAll();
  HBootMode::clearRequest();

  // Long enough for the lines above to leave the UART. A fault nobody can read
  // about is a fault reported as "it just reboots sometimes".
  HSystemUtils::sleep(50);

  esp_restart();
}

/** @brief The periodic check. Runs in the esp_timer task; does no work but compare. */
void onCheck(void* argument) {
  (void)argument;

  const char* faultedTask = nullptr;
  uint32_t silentFor = 0;

  {
    etl::lock_guard<etl::mutex> lock(registryMutex());

    if (!watching) {
      return;
    }

    const uint32_t now = HSystemUtils::millis();

    for (const Entry& entry : registry()) {
      // Unsigned subtraction, wrap-safe across the 49.7-day rollover of
      // millis() - the same rule HTimer follows.
      const uint32_t silence = now - entry.lastAliveMs;
      if (silence > entry.timeoutMs) {
        faultedTask = entry.task->name();
        silentFor = silence;
        break;
      }
    }
  }

  if (faultedTask == nullptr) {
    return;
  }

  // Logged outside the lock: HLog takes its own, and holding two in a fault
  // path is how a watchdog becomes the thing that hangs.
  HCritical("'%s' has not responded for %u ms - restarting cold", faultedTask,
            static_cast<unsigned>(silentFor));

  restartCold();
}

}  // namespace

HTaskManager& HTaskManager::instance() noexcept {
  static HTaskManager manager;
  return manager;
}

void HTaskManager::add(HTask& task, uint32_t timeoutMs) noexcept {
  if (timeoutMs == 0) {
    return;  // Not watched, by its own choice. See HTask::kNoWatchdog.
  }

  etl::lock_guard<etl::mutex> lock(registryMutex());
  Registry& entries = registry();

  for (Entry& entry : entries) {
    if (entry.task == &task) {
      entry.timeoutMs = timeoutMs;
      entry.lastAliveMs = HSystemUtils::millis();
      return;
    }
  }

  if (entries.full()) {
    HCritical("no room to watch '%s' - raise HTASKMANAGER_MAX_TASKS", task.name());
    return;
  }

  entries.push_back(Entry{&task, timeoutMs, HSystemUtils::millis()});
  HDebug("watching '%s' (%u ms)", task.name(), static_cast<unsigned>(timeoutMs));
}

void HTaskManager::remove(HTask& task) noexcept {
  etl::lock_guard<etl::mutex> lock(registryMutex());
  Registry& entries = registry();

  for (size_t index = 0; index < entries.size(); ++index) {
    if (entries[index].task == &task) {
      entries.erase(entries.begin() + index);
      HDebug("no longer watching '%s'", task.name());
      return;
    }
  }
}

void HTaskManager::alive(const HTask& task) noexcept {
  etl::lock_guard<etl::mutex> lock(registryMutex());

  for (Entry& entry : registry()) {
    if (entry.task == &task) {
      entry.lastAliveMs = HSystemUtils::millis();
      return;
    }
  }
}

void HTaskManager::start() noexcept {
  {
    etl::lock_guard<etl::mutex> lock(registryMutex());

    // Every task is treated as having just reported: whatever they were doing
    // before the watchdog existed is not something it can judge.
    const uint32_t now = HSystemUtils::millis();
    for (Entry& entry : registry()) {
      entry.lastAliveMs = now;
    }

    watching = true;
  }

  if (checkTimer == nullptr) {
    const esp_timer_create_args_t args = {.callback = &onCheck,
                                          .arg = nullptr,
                                          .dispatch_method = ESP_TIMER_TASK,
                                          .name = "taskwd",
                                          .skip_unhandled_events = true};

    if (esp_timer_create(&args, &checkTimer) != ESP_OK) {
      HCritical("could not create the check timer - nothing is being watched");
      return;
    }

    esp_timer_start_periodic(checkTimer, static_cast<uint64_t>(HTASKMANAGER_CHECK_MS) * 1000ULL);
  }

  HInfo("watching %u task(s), checking every %u ms",
        static_cast<unsigned>(watchedCount()), static_cast<unsigned>(HTASKMANAGER_CHECK_MS));
}

void HTaskManager::suspend() noexcept {
  etl::lock_guard<etl::mutex> lock(registryMutex());
  watching = false;
}

void HTaskManager::resume() noexcept {
  etl::lock_guard<etl::mutex> lock(registryMutex());

  const uint32_t now = HSystemUtils::millis();
  for (Entry& entry : registry()) {
    entry.lastAliveMs = now;
  }

  watching = true;
}

size_t HTaskManager::watchedCount() const noexcept {
  etl::lock_guard<etl::mutex> lock(registryMutex());
  return registry().size();
}
