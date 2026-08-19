#include "HCoreLibTest.hpp"

#include <cstdio>
#include <cstring>

// Before HLog.hpp, because the macros bake this in at the call site - which is
// the whole reason a module name costs nothing at run time.
#define HLOG_MODULE_NAME "HLogTest"
#include <HLog/HLog.hpp>

/**
 * @file HLogTest.cpp
 * @brief The front end: level filtering, the registry, and what a backend gets.
 *
 * Nothing here looks at the console backend's output. What is worth pinning is
 * the CONTRACT an application's own backend is written against - that a record
 * arrives fully formatted, that a disabled sink receives nothing, and that the
 * level check happens in the macro so a debug line below the threshold costs
 * not even its arguments.
 */

namespace {

/** @brief A sink that remembers the last record and counts them. */
class CaptureBackend : public HILogBackend {
 public:
  void write(const HLogRecord& record) noexcept override {
    ++count;
    level = record.level;
    line = record.line;
    hadFile = record.file != nullptr;
    std::snprintf(message, sizeof(message), "%s", record.message);
    std::snprintf(module, sizeof(module), "%s", record.module);
    std::snprintf(time, sizeof(time), "%s", record.time);
    std::snprintf(thread, sizeof(thread), "%s", record.thread);
  }

  void reset() noexcept {
    count = 0;
    message[0] = '\0';
  }

  int count = 0;
  HLogLevel level = HLogLevel::NO_LOG;
  int line = 0;
  bool hadFile = false;
  char message[128] = {};
  char module[32] = {};
  char time[32] = {};
  char thread[32] = {};
};

// Static storage duration, as HLog::addBackend() requires: the registry keeps
// a pointer and there is no way to unregister one.
CaptureBackend gCapture;
CaptureBackend gSecond;
CaptureBackend gThird;
CaptureBackend gOverflow;

int gCaptureIndex = HLog::INVALID_BACKEND_INDEX;

void checkRegistry() {
  // Index 0 is the console HLog registers by itself, so a fresh process has
  // exactly one backend and the first application sink lands at 1.
  CHECK(HLog::backendCount() == 1);

  gCaptureIndex = HLog::addBackend(gCapture);
  CHECK(gCaptureIndex == 1);
  CHECK(HLog::backendCount() == 2);
  CHECK(HLog::isBackendEnabled(static_cast<size_t>(gCaptureIndex)));

  // An unknown index changes nothing and admits it, rather than being clamped
  // onto a real backend.
  CHECK(!HLog::setBackendEnabled(99, false));
  CHECK(!HLog::isBackendEnabled(99));
}

void checkRecordContents() {
  HLog::setLevel(HLogLevel::DEBUG);
  gCapture.reset();

  HWarning("value is %d and %s", 42, "text");

  CHECK(gCapture.count == 1);
  CHECK(gCapture.level == HLogLevel::WARNING);
  CHECK_TEXT(gCapture.message, "value is 42 and text");
  CHECK_TEXT(gCapture.module, "HLogTest");

  // Never null, whatever the platform - a backend may print them unguarded.
  CHECK(std::strlen(gCapture.time) > 0);
  CHECK(std::strlen(gCapture.thread) > 0);

  // Source locations are captured on desktop debug builds only: they bloat a
  // flash image and leak host paths into the binary. Either way the record
  // must be self-consistent, which is what a backend relies on.
#if !defined(NDEBUG) && IS_DESKTOP
  CHECK(gCapture.hadFile);
  CHECK(gCapture.line > 0);
#else
  CHECK(!gCapture.hadFile);
#endif
}

/** @brief An over-long message is truncated, not allocated for. */
void checkTruncation() {
  HLog::setLevel(HLogLevel::DEBUG);
  gCapture.reset();

  char huge[600];
  std::memset(huge, 'x', sizeof(huge) - 1);
  huge[sizeof(huge) - 1] = '\0';

  HInfo("%s", huge);

  CHECK(gCapture.count == 1);
  CHECK(std::strlen(gCapture.message) < sizeof(huge) - 1);
}

/**
 * @brief The threshold, checked in the macro rather than in log().
 *
 * That is what makes a DEBUG line in a hot loop free in a release build: the
 * arguments are never evaluated, so the call never happens at all.
 */
void checkLevelFiltering() {
  gCapture.reset();
  HLog::setLevel(HLogLevel::WARNING);

  CHECK(HLog::getLevel() == HLogLevel::WARNING);

  int evaluated = 0;
  HDebug("%d", ++evaluated);
  HInfo("%d", ++evaluated);
  CHECK(gCapture.count == 0);
  CHECK(evaluated == 0);  // the arguments were not even computed

  HWarning("%d", ++evaluated);
  HCritical("%d", ++evaluated);
  CHECK(gCapture.count == 2);
  CHECK(evaluated == 2);

  gCapture.reset();
  HLog::setLevel(HLogLevel::NO_LOG);
  HCritical("silenced");
  CHECK(gCapture.count == 0);

  HLog::setLevel(HLogLevel::INFO);
}

void checkDisabledBackendsGetNothing() {
  gCapture.reset();
  CHECK(HLog::setBackendEnabled(static_cast<size_t>(gCaptureIndex), false));
  CHECK(!HLog::isBackendEnabled(static_cast<size_t>(gCaptureIndex)));

  HCritical("into the void");
  CHECK(gCapture.count == 0);

  CHECK(HLog::setBackendEnabled(static_cast<size_t>(gCaptureIndex), true));
  HCritical("heard");
  CHECK(gCapture.count == 1);
}

void checkFanOut() {
  gCapture.reset();
  gSecond.reset();

  CHECK(HLog::addBackend(gSecond) == 2);
  HCritical("to everyone");

  CHECK(gCapture.count == 1);
  CHECK(gSecond.count == 1);
  CHECK_TEXT(gSecond.message, "to everyone");
}

/**
 * @brief A full registry says so instead of growing.
 *
 * Runs last: there is no way to unregister a backend, so this permanently
 * fills the process's registry - which is exactly the state the check needs.
 */
void checkRegistryFull() {
  CHECK(HLog::addBackend(gThird) == 3);
  CHECK(HLog::backendCount() == 4);  // HLOG_MAX_BACKENDS
  CHECK(HLog::addBackend(gOverflow) == HLog::INVALID_BACKEND_INDEX);
  CHECK(HLog::backendCount() == 4);
}

}  // namespace

void runLogTests() noexcept {
  HCoreLibTest::begin("HLog");

  checkRegistry();
  checkRecordContents();
  checkTruncation();
  checkLevelFiltering();
  checkDisabledBackendsGetNothing();
  checkFanOut();
  checkRegistryFull();

  // The capture sinks outlive this suite and every later one logs. Silencing
  // them here keeps a warning from HConfig's rejection tests out of a buffer
  // nobody is looking at any more.
  for (size_t i = 1; i < HLog::backendCount(); ++i) {
    HLog::setBackendEnabled(i, false);
  }
  HLog::setLevel(HLogLevel::INFO);
}
