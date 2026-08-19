#pragma once

#include <HCoreLib.h>

#include <cstddef>

#include "HILogBackend/HILogBackend.hpp"
#include "HLogLevel/HLogLevel.hpp"

#ifndef HLOG_MODULE_NAME
#define HLOG_MODULE_NAME "UNKNOWN"
#endif

// Lets the compiler type-check log() arguments against the format string, the
// same way it does for printf. GNU-only, so it folds away elsewhere.
#if defined(__GNUC__)
#define HLOG_PRINTF_LIKE_(fmtIndex, firstArgIndex) __attribute__((format(printf, fmtIndex, firstArgIndex)))
#else
#define HLOG_PRINTF_LIKE_(fmtIndex, firstArgIndex)
#endif

/**
 * @brief Log front end: level filtering, formatting and backend fan-out.
 *
 * Never instantiated - all state is global, because logging must work from any
 * task without being handed an object. Use the HDebug/HInfo/HWarning/HCritical
 * macros rather than log(): they skip the whole call, including argument
 * evaluation, when the record is below the current level.
 *
 * Backends are addressed by index. Index 0 (CONSOLE_BACKEND_INDEX) is a
 * console backend HLog registers on its own and keeps enabled; an application
 * adds its own sinks with addBackend() and can silence any of them - the
 * console included - with setBackendEnabled(). Registration is bounded by
 * HLOG_MAX_BACKENDS and allocates nothing.
 */
class HLog {
 public:
  HLog() = delete;

  /// Index of the built-in console (printf) backend, always present.
  static constexpr size_t CONSOLE_BACKEND_INDEX = 0;

  /// Returned by addBackend() when the registry is full.
  static constexpr int INVALID_BACKEND_INDEX = -1;

  /**
   * @brief Sets the minimum severity that is emitted. Defaults to INFO.
   * @param level Records below this level are dropped; NO_LOG drops all.
   */
  static void setLevel(HLogLevel level) noexcept;

  /**
   * @brief Returns the current minimum severity.
   */
  static HLogLevel getLevel() noexcept;

  /**
   * @brief Registers a sink and enables it.
   * @param backend Sink to add. Must outlive every later log call, so give it
   *                static storage duration.
   * @return Index to control the backend with, or INVALID_BACKEND_INDEX when
   *         HLOG_MAX_BACKENDS is already reached.
   */
  static int addBackend(HILogBackend& backend) noexcept;

  /**
   * @brief Number of registered backends, always at least 1 (the console).
   */
  static size_t backendCount() noexcept;

  /**
   * @brief Turns one backend on or off without unregistering it.
   * @param index Index returned by addBackend(), or CONSOLE_BACKEND_INDEX.
   * @param enabled True to emit to it again, false to silence it.
   * @return False when index is out of range, in which case nothing changed.
   */
  static bool setBackendEnabled(size_t index, bool enabled) noexcept;

  /**
   * @brief Tells whether a backend currently receives records.
   * @param index Index to query.
   * @return False for an unknown index as well as for a disabled backend.
   */
  static bool isBackendEnabled(size_t index) noexcept;

  /**
   * @brief Formats one record and hands it to every enabled backend.
   *
   * Prefer the HDebug/HInfo/HWarning/HCritical macros - they fill in module,
   * file and line and skip the call below the current level.
   *
   * @param level Severity of the record.
   * @param module Name of the emitting module (HLOG_MODULE_NAME).
   * @param file Source file, or nullptr to omit the source location.
   * @param line Source line, ignored when file is nullptr.
   * @param fmt printf-style format, followed by its arguments. The result is
   *            truncated at HLOG_MESSAGE_MAX_LENGTH characters.
   */
  HLOG_PRINTF_LIKE_(5, 6)
  static void log(HLogLevel level, const char* module, const char* file, int line, const char* fmt, ...) noexcept;
};

// Source locations bloat the flash image and leak host paths into the binary,
// so they are only captured on desktop debug builds.
#if !defined(NDEBUG) && IS_DESKTOP
#define HLOG_INTERNAL_(level, fmt, ...) \
  HLog::log(level, HLOG_MODULE_NAME, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define HLOG_INTERNAL_(level, fmt, ...) \
  HLog::log(level, HLOG_MODULE_NAME, nullptr, 0, fmt, ##__VA_ARGS__)
#endif

#define HDebug(fmt, ...) \
  do { \
    if (HLog::getLevel() <= HLogLevel::DEBUG) { \
      HLOG_INTERNAL_(HLogLevel::DEBUG, fmt, ##__VA_ARGS__); \
    } \
  } while (0)

#define HInfo(fmt, ...) \
  do { \
    if (HLog::getLevel() <= HLogLevel::INFO) { \
      HLOG_INTERNAL_(HLogLevel::INFO, fmt, ##__VA_ARGS__); \
    } \
  } while (0)

#define HWarning(fmt, ...) \
  do { \
    if (HLog::getLevel() <= HLogLevel::WARNING) { \
      HLOG_INTERNAL_(HLogLevel::WARNING, fmt, ##__VA_ARGS__); \
    } \
  } while (0)

#define HCritical(fmt, ...) \
  do { \
    if (HLog::getLevel() <= HLogLevel::CRITICAL) { \
      HLOG_INTERNAL_(HLogLevel::CRITICAL, fmt, ##__VA_ARGS__); \
    } \
  } while (0)
