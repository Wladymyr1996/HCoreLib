#include "HCoreLibTest.hpp"

#include <cstdio>
#include <cstring>

#include <HJson/HJson.hpp>

/**
 * @file HJsonTest.cpp
 * @brief The DOM, the bump allocator, and what happens when it runs out.
 *
 * The exhaustion cases matter more here than the happy path. HJson exists
 * because the portal has to answer a request without a heap, so the interesting
 * question is never "does it parse valid JSON" - it is what a document does when
 * the caller's buffer is not big enough for what arrived on the socket, which on
 * a device is a Tuesday rather than an edge case.
 */

namespace {

void checkParsing() {
  char pool[1024];
  HJsonDocument doc(pool, sizeof(pool));

  REQUIRE(doc.parse("{\"name\":\"garage\",\"travelMs\":4080,\"ratio\":2.5,"
                    "\"enabled\":true,\"missing\":null,"
                    "\"tags\":[\"a\",\"b\"],\"nested\":{\"deep\":7}}"));

  const HJsonValue& root = doc.getRoot();
  CHECK(root.type() == HJsonValue::Type::Object);
  CHECK(root.size() == 7);

  CHECK(root["name"].asString() == std::string_view("garage"));
  CHECK(root["travelMs"].asInt() == 4080);
  CHECK(root["travelMs"].type() == HJsonValue::Type::Int);
  CHECK(root["ratio"].type() == HJsonValue::Type::Double);
  CHECK(root["ratio"].asDouble() > 2.49 && root["ratio"].asDouble() < 2.51);
  CHECK(root["enabled"].asBool());
  CHECK(root["missing"].isNull());

  CHECK(root["tags"].type() == HJsonValue::Type::Array);
  CHECK(root["tags"].size() == 2);
  CHECK(root["tags"].at(0).asString() == std::string_view("a"));
  CHECK(root["tags"].at(1).asString() == std::string_view("b"));

  CHECK(root["nested"]["deep"].asInt() == 7);
}

/**
 * @brief The Null Object Pattern, which is what makes chaining safe.
 *
 * `doc["a"]["b"]["c"]` on a document that has none of them has to answer
 * rather than crash, because the alternative is every REST handler checking
 * every level of every request body by hand.
 */
void checkMissingLookups() {
  char pool[512];
  HJsonDocument doc(pool, sizeof(pool));
  REQUIRE(doc.parse("{\"a\":{\"b\":1},\"arr\":[10]}"));

  const HJsonValue& root = doc.getRoot();
  CHECK(root["nope"].isNull());
  CHECK(root["nope"]["deeper"]["deeper still"].isNull());
  CHECK(root["nope"].asInt() == 0);
  CHECK(root["nope"].asString().empty());

  CHECK(root["arr"].at(5).isNull());
  CHECK(root["a"].at(0).isNull());       // an Object has no elements
  CHECK(root["a"]["b"].size() == 0);     // a scalar has no children
}

void checkRejectsMalformed() {
  char pool[512];
  HJsonDocument doc(pool, sizeof(pool));

  CHECK(!doc.parse("{\"unterminated\": "));
  CHECK(!doc.parse("{\"key\" 1}"));
  CHECK(!doc.parse("[1, 2"));
  CHECK(!doc.parse("nonsense"));
  CHECK(!doc.parse(""));
}

void checkBuildingAndSerializing() {
  char pool[1024];
  HJsonDocument doc(pool, sizeof(pool));

  HJsonValue& root = doc.getRoot();
  root["ok"] = true;
  root["count"] = 3;
  root["name"] = "door1";

  HJsonValue& list = root["units"];
  list.pushBack() = 1;
  list.pushBack() = 2;

  char text[256];
  const size_t written = doc.serialize(text, sizeof(text));
  CHECK(written == std::strlen(text));

  // Order is append order, because children are linked at the tail - which is
  // what makes this comparable against a literal at all.
  CHECK_TEXT(text, "{\"ok\":true,\"count\":3,\"name\":\"door1\",\"units\":[1,2]}");
}

/** @brief Text that has to come back out as it went in. */
void checkEscaping() {
  char pool[512];
  HJsonDocument doc(pool, sizeof(pool));

  doc.getRoot()["text"] = "a\"b\\c\nd\te";

  char text[128];
  doc.serialize(text, sizeof(text));
  CHECK_TEXT(text, "{\"text\":\"a\\\"b\\\\c\\nd\\te\"}");

  // And a full round trip: parsing what was just written must rebuild the
  // original bytes, not the escaped ones.
  char reparsePool[512];
  HJsonDocument reparsed(reparsePool, sizeof(reparsePool));
  REQUIRE(reparsed.parse(text));
  CHECK(reparsed.getRoot()["text"].asString() == std::string_view("a\"b\\c\nd\te"));
}

/** @brief A double must not come back as an Int, or a config file changes type. */
void checkNumberRoundTrip() {
  char pool[512];
  HJsonDocument doc(pool, sizeof(pool));

  doc.getRoot()["whole"] = 5.0;
  doc.getRoot()["fraction"] = 0.25;
  doc.getRoot()["integer"] = 5;

  char text[128];
  doc.serialize(text, sizeof(text));

  char reparsePool[512];
  HJsonDocument reparsed(reparsePool, sizeof(reparsePool));
  REQUIRE(reparsed.parse(text));
  CHECK(reparsed.getRoot()["whole"].type() == HJsonValue::Type::Double);
  CHECK(reparsed.getRoot()["fraction"].type() == HJsonValue::Type::Double);
  CHECK(reparsed.getRoot()["integer"].type() == HJsonValue::Type::Int);
}

/** @brief Serializing into a buffer that cannot hold the document. */
void checkSerializeTruncates() {
  char pool[512];
  HJsonDocument doc(pool, sizeof(pool));
  REQUIRE(doc.parse("{\"aaa\":1,\"bbb\":2}"));

  char small[8];
  std::memset(small, 'X', sizeof(small));
  const size_t written = doc.serialize(small, sizeof(small));

  CHECK(written == sizeof(small) - 1);
  CHECK(small[sizeof(small) - 1] == '\0');
  CHECK(std::strncmp(small, "{\"aaa\":", sizeof(small) - 1) == 0);

  // Zero-sized output writes nothing at all rather than a stray terminator.
  char untouched = 'Z';
  CHECK(doc.serialize(&untouched, 0) == 0);
  CHECK(untouched == 'Z');
}

/**
 * @brief Running the pool dry, which must fail rather than overrun.
 *
 * parse() reclaims the whole buffer first, so the document is reusable
 * afterwards - the check below is that a failed parse is recoverable, not just
 * survivable.
 */
void checkPoolExhaustion() {
  char pool[128];
  HJsonDocument doc(pool, sizeof(pool));

  CHECK(!doc.parse("{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,"
                   "\"h\":8,\"i\":9,\"j\":10,\"k\":11,\"l\":12}"));
  CHECK(doc.usedBytes() <= doc.capacityBytes());

  REQUIRE(doc.parse("{\"a\":1}"));
  CHECK(doc.getRoot()["a"].asInt() == 1);

  // Mutation past the end returns the shared Null node instead of scribbling.
  HJsonDocument tiny(pool, 24);
  HJsonValue& root = tiny.getRoot();
  for (int i = 0; i < 20; ++i) {
    char key[8];
    std::snprintf(key, sizeof(key), "k%d", i);
    root[key] = i;
  }
  CHECK(tiny.usedBytes() <= tiny.capacityBytes());
}

}  // namespace

void runJsonTests() noexcept {
  HCoreLibTest::begin("HJson");

  checkParsing();
  checkMissingLookups();
  checkRejectsMalformed();
  checkBuildingAndSerializing();
  checkEscaping();
  checkNumberRoundTrip();
  checkSerializeTruncates();
  checkPoolExhaustion();
}
