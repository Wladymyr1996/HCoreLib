#include "HConsoleLogBackend.hpp"

#include <cstdio>

// ANSI colors
#define GREEN_COLOR  "\033[32m"
#define ORANGE_COLOR "\033[33m"
#define RED_COLOR    "\033[31m"
#define GRAY_COLOR   "\033[90m"
#define RESET_COLOR  "\033[0m"

namespace {

/**
 * @brief Maps a severity to the escape sequence its tag is printed in.
 */
const char* levelToColor(HLogLevel level) noexcept {
  switch (level) {
    case HLogLevel::DEBUG:
      return GRAY_COLOR;
    case HLogLevel::INFO:
      return GREEN_COLOR;
    case HLogLevel::WARNING:
      return ORANGE_COLOR;
    case HLogLevel::CRITICAL:
      return RED_COLOR;
    default:
      return RESET_COLOR;
  }
}

}  // namespace

void HConsoleLogBackend::write(const HLogRecord& record) noexcept {
  printf(GREEN_COLOR "[%s]%s[%s]" RESET_COLOR "[%s][%s]",
         record.time, levelToColor(record.level), hLogLevelToTag(record.level),
         record.module, record.thread);

  // Source location is only captured on desktop debug builds - see HLog.hpp.
  if (record.file != nullptr) {
    printf(GRAY_COLOR "(%s:%d)" RESET_COLOR, record.file, record.line);
  }

  printf(": %s\n", record.message);
  fflush(stdout);
}
