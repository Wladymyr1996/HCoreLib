#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @file HCoreLibTest.hpp
 * @brief A test harness small enough to not need explaining.
 *
 * No framework, and the same one HAPLib's suite uses - a host suite that needs
 * a dependency has stopped being a thing you can run in a second on any
 * machine, and the whole point of testing this library on a desktop is that
 * doing so costs nothing. It is also what makes the CI job three lines of
 * cmake with nothing to download.
 *
 * CHECK_TEXT is the one addition that earns its place here. HConfig's file
 * format is a contract with every device already in the field, so a test that
 * compares a written file against the exact text it should contain - and
 * prints both when they differ - is what keeps a refactor from quietly
 * reshaping files a running device has to keep reading.
 */
namespace HCoreLibTest {

/** @brief Starts a named group; every check after this belongs to it. */
void begin(const char* suite) noexcept;

/** @brief Records one assertion. Called through CHECK. */
bool check(bool passed, const char* expression, int line) noexcept;

/**
 * @brief Compares two null-terminated strings, printing both on a mismatch.
 *
 * Line-by-line, with the first differing line called out: "the file is not
 * what it should be" is not a debuggable message for something an application
 * has to parse.
 */
bool checkText(const char* actual, const char* expected, const char* what,
               int line) noexcept;

/** @brief Compares floats with a tolerance; exact equality is not a thing here. */
bool checkNear(float actual, float expected, float tolerance, const char* what,
               int line) noexcept;

/**
 * @brief Reads a whole file through HFs into `buffer`, null-terminating it.
 *
 * Opened BINARY, like everything else that touches a config file: a text-mode
 * stream on Windows would turn the "\n" the writer emitted into "\r\n" on the
 * way back and fail every format check on that platform alone.
 * @return Bytes read, or 0 if the file could not be opened or does not fit.
 */
size_t readTextFile(const char* path, char* buffer, size_t bufferSize) noexcept;

/** @brief Writes `text` to `path` verbatim, creating or truncating it. */
bool writeTextFile(const char* path, const char* text) noexcept;

/** @brief Deletes `config/` and everything in it, so a suite starts clean. */
void clearConfigDir() noexcept;

/** @brief Prints the totals. @return A process exit code: 0 when all passed. */
int report() noexcept;

}  // namespace HCoreLibTest

#define CHECK(expression) HCoreLibTest::check((expression), #expression, __LINE__)

/**
 * @brief A check the rest of the test cannot survive - a pointer, usually.
 *
 * CHECK records a failure and carries on, which is what you want for an
 * assertion about a value. It is exactly what you do NOT want after `p !=
 * nullptr`: the next line dereferences it and the whole run dies with a
 * segfault instead of a failure report naming the line.
 */
#define REQUIRE(expression)                                       \
  do {                                                            \
    if (!HCoreLibTest::check((expression), #expression, __LINE__)) { \
      return;                                                     \
    }                                                             \
  } while (false)

#define CHECK_TEXT(actual, expected) \
  HCoreLibTest::checkText((actual), (expected), #actual, __LINE__)

#define CHECK_NEAR(actual, expected, tolerance) \
  HCoreLibTest::checkNear((actual), (expected), (tolerance), #actual, __LINE__)

void runValueTests() noexcept;
void runJsonTests() noexcept;
void runConfigPathTests() noexcept;
void runConfigTests() noexcept;
void runLogTests() noexcept;
void runSha256Tests() noexcept;
void runTimerTests() noexcept;
void runHookListTests() noexcept;
void runFsTests() noexcept;
void runGpioTests() noexcept;
void runButtonTests() noexcept;
void runRtcStoreTests() noexcept;
void runAuthTests() noexcept;
