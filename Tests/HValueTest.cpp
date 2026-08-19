#include "HCoreLibTest.hpp"

#include <cstring>

#include <HValue/HValue.hpp>

/**
 * @file HValueTest.cpp
 * @brief HValue's two rules, which everything above it depends on.
 *
 * The type is fixed at construction, and every assignment COERCES into it.
 * Those two sentences are why HConfigEntry needs a hand-written operator= and
 * why hConfigValueFromText() constructs before it assigns - both of which are
 * bugs waiting to come back if this file ever stops holding the line.
 */

namespace {

/** @brief The longest string that fits, plus one character that does not. */
void checkTruncation() {
  char tooLong[HVALUE_MAX_STRING_LEN + 8];
  for (size_t i = 0; i < sizeof(tooLong) - 1; ++i) {
    tooLong[i] = static_cast<char>('a' + (i % 26));
  }
  tooLong[sizeof(tooLong) - 1] = '\0';

  const HValue value(tooLong);
  CHECK(value.asString().size() == HVALUE_MAX_STRING_LEN);
  CHECK(std::strncmp(value.asString().c_str(), tooLong, HVALUE_MAX_STRING_LEN) == 0);
}

void checkTypesAreFixed() {
  HValue intValue(7);
  CHECK(intValue.isInt());

  // The whole point: a string lands in an Int slot as a PARSED int, and the
  // slot is still an Int afterwards.
  intValue = "42";
  CHECK(intValue.isInt());
  CHECK(intValue.asInt() == 42);

  intValue = 3.9f;
  CHECK(intValue.isInt());
  CHECK(intValue.asInt() == 3);

  HValue stringValue("hello");
  stringValue = 12;
  CHECK(stringValue.isString());
  CHECK(std::strcmp(stringValue.asString().c_str(), "12") == 0);

  // Copy-assignment coerces too - it does not adopt the source's type. This is
  // the trap HConfigEntry::operator= exists to avoid.
  HValue target(0);
  const HValue source("55");
  target = source;
  CHECK(target.isInt());
  CHECK(target.asInt() == 55);

  // Copy-CONSTRUCTION is the one that adopts it.
  const HValue copy(source);
  CHECK(copy.isString());
}

void checkCoercions() {
  CHECK(HValue("42abc").asInt() == 42);
  CHECK(HValue("abc").asInt() == 0);
  CHECK(HValue("").asInt() == 0);
  CHECK(HValue().asInt() == 0);
  CHECK(HValue(true).asInt() == 1);
  CHECK(HValue(-2.7f).asInt() == -2);

  CHECK_NEAR(HValue("2.5xyz").asFloat(), 2.5f, 0.0001f);
  CHECK_NEAR(HValue("nonsense").asFloat(), 0.0f, 0.0001f);
  CHECK_NEAR(HValue(3).asFloat(), 3.0f, 0.0001f);

  // Only "true" and "1" are true. "yes", "TRUE" and "2" are not, deliberately.
  CHECK(HValue("true").asBool());
  CHECK(HValue("1").asBool());
  CHECK(!HValue("TRUE").asBool());
  CHECK(!HValue("yes").asBool());
  CHECK(!HValue("false").asBool());
  CHECK(!HValue("0").asBool());
  CHECK(!HValue().asBool());
  CHECK(HValue(-1).asBool());
  CHECK(!HValue(0.0f).asBool());

  CHECK(std::strcmp(HValue(-8).asString().c_str(), "-8") == 0);
  CHECK(std::strcmp(HValue(true).asString().c_str(), "true") == 0);
  CHECK(std::strcmp(HValue(false).asString().c_str(), "false") == 0);
  CHECK(std::strcmp(HValue().asString().c_str(), "") == 0);
  CHECK(std::strcmp(HValue(2.5f).asString().c_str(), "2.5") == 0);

  // nullptr is an empty string, not a crash and not a Null.
  const HValue fromNull(static_cast<const char*>(nullptr));
  CHECK(fromNull.isString());
  CHECK(fromNull.asString().empty());
}

void checkEquality() {
  CHECK(HValue(1) == HValue(1));
  CHECK(HValue("a") == HValue("a"));
  CHECK(HValue() == HValue());

  // Strict: a type mismatch is never equal, however convertible the values.
  CHECK(HValue(1) != HValue(1.0f));
  CHECK(HValue(1) != HValue("1"));
  CHECK(HValue(true) != HValue(1));
  CHECK(HValue("a") != HValue("b"));
}

}  // namespace

void runValueTests() noexcept {
  HCoreLibTest::begin("HValue");

  checkTypesAreFixed();
  checkCoercions();
  checkEquality();
  checkTruncation();
}
