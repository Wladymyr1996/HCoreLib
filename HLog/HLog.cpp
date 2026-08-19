#include "HLog.hpp"

#include <cstdarg>
#include <cstddef>
#include <cstdio>

#include <etl/mutex.h>
#include <etl/vector.h>

#include <HCoreLib.h>
#include <HSystemUtils/HSystemUtils.hpp>

#include "HConsoleLogBackend/HConsoleLogBackend.hpp"
#include "HLogRecord/HLogRecord.hpp"

// Both limits are compile-time budgets: no heap is used, so they decide how
// much static and stack memory logging costs. Override them from CMake with
// target_compile_definitions() if a project needs more.
#ifndef HLOG_MAX_BACKENDS
#define HLOG_MAX_BACKENDS 4
#endif

#ifndef HLOG_MESSAGE_MAX_LENGTH
#define HLOG_MESSAGE_MAX_LENGTH 256
#endif

namespace {

/// A registered sink plus the flag the application toggles it with.
struct BackendSlot {
  HILogBackend* backend;
  bool enabled;
};

using BackendRegistry = etl::vector<BackendSlot, HLOG_MAX_BACKENDS>;

HLogLevel currentLevel = HLogLevel::INFO;

/**
 * @brief The backend registry, built on first use with the console at index 0.
 *
 * Deliberately a function-local static rather than a global: a log call coming
 * from another translation unit's static constructor would otherwise race the
 * registry's own construction, and static init order across TUs is not defined.
 */
BackendRegistry& registry() noexcept {
  static HConsoleLogBackend consoleBackend;
  static BackendRegistry backends(1U, BackendSlot{&consoleBackend, true});
  return backends;
}

/**
 * @brief Serialises log() so records from different tasks do not interleave.
 *
 * Function-local for the same init-order reason as registry(). etl::mutex maps
 * to a static FreeRTOS mutex on the target and to std::mutex on desktop.
 */
etl::mutex& logMutex() noexcept {
  static etl::mutex mutex;
  return mutex;
}

}  // namespace

void HLog::setLevel(HLogLevel level) noexcept {
  currentLevel = level;
}

HLogLevel HLog::getLevel() noexcept {
  return currentLevel;
}

int HLog::addBackend(HILogBackend& backend) noexcept {
  etl::lock_guard<etl::mutex> lock(logMutex());

  BackendRegistry& backends = registry();
  if (backends.full()) {
    return INVALID_BACKEND_INDEX;
  }

  backends.push_back(BackendSlot{&backend, true});
  return static_cast<int>(backends.size() - 1U);
}

size_t HLog::backendCount() noexcept {
  etl::lock_guard<etl::mutex> lock(logMutex());

  return registry().size();
}

bool HLog::setBackendEnabled(size_t index, bool enabled) noexcept {
  etl::lock_guard<etl::mutex> lock(logMutex());

  BackendRegistry& backends = registry();
  if (index >= backends.size()) {
    return false;
  }

  backends[index].enabled = enabled;
  return true;
}

bool HLog::isBackendEnabled(size_t index) noexcept {
  etl::lock_guard<etl::mutex> lock(logMutex());

  const BackendRegistry& backends = registry();
  return (index < backends.size()) && backends[index].enabled;
}

void HLog::log(HLogLevel level, const char* module, const char* file, int line, const char* fmt, ...) noexcept {
  char timeBuf[24];
  char threadBuf[20];
  char messageBuf[HLOG_MESSAGE_MAX_LENGTH];

  HSystemUtils::getLogTime(timeBuf, sizeof(timeBuf));
  HSystemUtils::getThreadId(threadBuf, sizeof(threadBuf));

  // Formatted once here instead of once per backend; over-long messages are
  // truncated rather than allocated for.
  va_list args;
  va_start(args, fmt);
  vsnprintf(messageBuf, sizeof(messageBuf), fmt, args);
  va_end(args);

  const HLogRecord record{level, timeBuf, module, threadBuf, file, line, messageBuf};

  etl::lock_guard<etl::mutex> lock(logMutex());

  for (const BackendSlot& slot : registry()) {
    if (slot.enabled) {
      slot.backend->write(record);
    }
  }
}
