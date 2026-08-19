#include "HCoreLibTest.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <etl/delegate.h>

#include <HFs/HFs.hpp>
#include <HLog/HLog.hpp>

namespace {

const char* gSuite = "";
int gChecks = 0;
int gFailures = 0;

/** @brief Prints one string with its line breaks made visible. */
void dump(const char* label, const char* text) noexcept {
  std::printf("      %s: \"", label);
  for (const char* c = text; *c != '\0'; ++c) {
    if (*c == '\n') {
      std::printf("\\n");
    } else if (*c == '\r') {
      std::printf("\\r");
    } else {
      std::putchar(*c);
    }
  }
  std::printf("\"\n");
}

/** @brief Collects the names in config/ so clearConfigDir() can delete them. */
struct DirCollector {
  static constexpr size_t kMaxEntries = 16;
  static constexpr size_t kMaxName = 32;

  char names[kMaxEntries][kMaxName] = {};
  size_t count = 0;

  bool onEntry(const char* name, bool isDirectory) noexcept {
    if (isDirectory || count >= kMaxEntries || std::strlen(name) >= kMaxName) {
      return true;
    }
    std::snprintf(names[count], kMaxName, "%s", name);
    ++count;
    return true;
  }
};

DirCollector gCollector;

bool collectEntry(const char* name, bool isDirectory) noexcept {
  return gCollector.onEntry(name, isDirectory);
}

}  // namespace

namespace HCoreLibTest {

void begin(const char* suite) noexcept {
  gSuite = suite;
  std::printf("\n%s\n", suite);
}

bool check(bool passed, const char* expression, int line) noexcept {
  ++gChecks;

  if (passed) {
    return true;
  }

  ++gFailures;
  std::printf("  FAIL %s line %d: %s\n", gSuite, line, expression);
  return false;
}

bool checkText(const char* actual, const char* expected, const char* what,
               int line) noexcept {
  ++gChecks;

  const char* const safeActual = (actual != nullptr) ? actual : "(null)";
  const char* const safeExpected = (expected != nullptr) ? expected : "(null)";

  if (std::strcmp(safeActual, safeExpected) == 0) {
    return true;
  }

  ++gFailures;
  std::printf("  FAIL %s line %d: %s\n", gSuite, line, what);

  // The offset alone is enough to find it in text this short, and printing it
  // costs nothing next to the two dumps below.
  size_t offset = 0;
  while (safeActual[offset] != '\0' && safeActual[offset] == safeExpected[offset]) {
    ++offset;
  }
  std::printf("      first difference at offset %zu\n", offset);

  dump("expected", safeExpected);
  dump("actual  ", safeActual);
  return false;
}

bool checkNear(float actual, float expected, float tolerance, const char* what,
               int line) noexcept {
  ++gChecks;

  if (std::fabs(actual - expected) <= tolerance) {
    return true;
  }

  ++gFailures;
  std::printf("  FAIL %s line %d: %s\n", gSuite, line, what);
  std::printf("      expected %g +/- %g, got %g\n", static_cast<double>(expected),
              static_cast<double>(tolerance), static_cast<double>(actual));
  return false;
}

size_t readTextFile(const char* path, char* buffer, size_t bufferSize) noexcept {
  if (buffer == nullptr || bufferSize == 0) {
    return 0;
  }
  buffer[0] = '\0';

  HFs::HFile file;
  if (!HFs::HFileSystem::openFile(path, file, "rb")) {
    return 0;
  }

  const size_t read = file.read(buffer, bufferSize - 1);
  file.close();
  buffer[read] = '\0';
  return read;
}

bool writeTextFile(const char* path, const char* text) noexcept {
  HFs::HFile file;
  if (!HFs::HFileSystem::openFile(path, file, "wb")) {
    return false;
  }

  const size_t length = std::strlen(text);
  const bool written = file.write(text, length) == length;
  file.close();
  return written;
}

void clearConfigDir() noexcept {
  if (!HFs::HFileSystem::isDirectory("config")) {
    return;
  }

  gCollector.count = 0;
  HFs::HFileSystem::listDir("config", HFsEntryVisitor::create<&collectEntry>());

  for (size_t i = 0; i < gCollector.count; ++i) {
    char path[80];
    std::snprintf(path, sizeof(path), "config/%s", gCollector.names[i]);
    HFs::HFileSystem::deleteFile(path);
  }
}

int report() noexcept {
  std::printf("\n%d checks, %d failed\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}

}  // namespace HCoreLibTest

int main() {
  // Every backend of the library refuses to work before the filesystem is up,
  // and on the host that is a flag rather than a mount - see HFsDesktop.
  if (!HFs::HFileSystem::mount()) {
    std::printf("could not mount the host filesystem\n");
    return 1;
  }

  // Several suites deliberately feed HConfig and HAuth input they must reject,
  // and every rejection is a warning on stdout. Silencing the console backend
  // keeps the run readable; HLogTest turns it back off after exercising it.
  HLog::setBackendEnabled(HLog::CONSOLE_BACKEND_INDEX, false);

  runValueTests();
  runJsonTests();
  runConfigPathTests();
  runConfigTests();
  runLogTests();
  runSha256Tests();
  runTimerTests();
  runHookListTests();
  runFsTests();
  runGpioTests();
  runButtonTests();
  runRtcStoreTests();
  runAuthTests();

  return HCoreLibTest::report();
}
