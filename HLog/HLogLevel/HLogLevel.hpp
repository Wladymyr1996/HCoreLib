#pragma once

#include <cstdint>

/**
 * @brief Severity of a log record, ordered from most to least verbose.
 *
 * The numeric order is what HLog compares against its threshold, so new levels
 * must keep the ascending-severity ordering. NO_LOG sits above every real level
 * and therefore silences everything when used as the threshold.
 */
enum class HLogLevel : uint8_t {
  DEBUG = 0,
  INFO = 1,
  WARNING = 2,
  CRITICAL = 3,
  NO_LOG = 99
};

/**
 * @brief Returns the printable tag of a level ("DEBUG", "INFO", ...).
 * @param level Level to describe.
 * @return Static string, never nullptr. Unknown values map to "UNKNOWN".
 */
inline const char* hLogLevelToTag(HLogLevel level) noexcept {
  switch (level) {
    case HLogLevel::DEBUG:
      return "DEBUG";
    case HLogLevel::INFO:
      return "INFO";
    case HLogLevel::WARNING:
      return "WARNING";
    case HLogLevel::CRITICAL:
      return "CRITICAL";
    default:
      return "UNKNOWN";
  }
}
