#include "HCoreLibTest.hpp"

#include <cstring>

#include <HConfig/HConfigPath.hpp>

/**
 * @file HConfigPathTest.cpp
 * @brief Paths, and the numeric ordering the writer's one pass depends on.
 *
 * The ordering rule is the load-bearing one. HConfig::write() emits a nested
 * file in a single pass, which is only correct while siblings are contiguous,
 * which is only true while "/a/2" sorts before "/a/10". Lexicographic order
 * gets that backwards, so this is the check that keeps a file with eleven
 * array elements from growing two `units [ao]:` blocks.
 */

namespace {

void checkParsing() {
  HConfigPath path;

  // Leading and trailing slashes are noise; the segments are the path.
  REQUIRE(path.parse("/units/0/name"));
  CHECK(path.valid());
  CHECK(path.size() == 3);
  CHECK(std::strcmp(path.segment(0), "units") == 0);
  CHECK(path.isIndex(1));
  CHECK(path.index(1) == 0);
  CHECK(!path.isIndex(2));
  CHECK(path.index(2) == 0);

  HConfigPath bare;
  REQUIRE(bare.parse("travelMs"));
  CHECK(bare.size() == 1);
  CHECK(bare == HConfigPath("/travelMs"));
  CHECK(bare == HConfigPath("/travelMs/"));

  HConfigPath root;
  CHECK(root.empty());
  CHECK(root.size() == 0);

  // Out of range is "" and not a crash: a caller walking a path it did not
  // build should get an empty answer rather than a fault.
  CHECK(std::strcmp(path.segment(99), "") == 0);
}

/**
 * @brief What must be REJECTED, not truncated.
 *
 * A shortened path is not an invalid path - it is a different one, quite
 * possibly a real one, and writing the wrong setting is exactly the failure
 * this class exists to prevent.
 */
void checkRejections() {
  HConfigPath path;

  CHECK(!path.parse("/a//b"));
  CHECK(!path.valid());
  CHECK(path.empty());

  char longSegment[HCONFIG_MAX_CONFIG_NAME_LEN + 8];
  std::memset(longSegment, 'x', sizeof(longSegment) - 1);
  longSegment[sizeof(longSegment) - 1] = '\0';
  CHECK(!path.parse(longSegment));

  // One segment past HCONFIG_MAX_DEPTH.
  char tooDeep[HCONFIG_MAX_DEPTH * 2 + 4] = "";
  for (size_t i = 0; i <= HCONFIG_MAX_DEPTH; ++i) {
    std::strcat(tooDeep, "/a");
  }
  CHECK(!path.parse(tooDeep));

  // Empty text is not a rejection: it is the ROOT, which is a path HConfig
  // takes as an argument - `count(module, "")` counts the file's top level.
  REQUIRE(path.parse(""));
  CHECK(path.valid());
  CHECK(path.empty());

  REQUIRE(path.parse("/"));
  CHECK(path.empty());
}

void checkPushAndPop() {
  HConfigPath path;

  CHECK(path.push("units"));
  CHECK(path.pushIndex(3));
  CHECK(path.push("name"));
  CHECK(path.size() == 3);
  CHECK(path.isIndex(1));
  CHECK(path.index(1) == 3);

  path.pop();
  CHECK(path.size() == 2);
  CHECK(path == HConfigPath("/units/3"));

  path.clear();
  CHECK(path.empty());
  path.pop();  // popping the root does nothing rather than misbehaving
  CHECK(path.empty());

  CHECK(!path.push(""));
  CHECK(path.empty());

  // The depth limit applies to push() as well as to parse().
  for (size_t i = 0; i < HCONFIG_MAX_DEPTH; ++i) {
    CHECK(path.push("a"));
  }
  CHECK(!path.push("a"));
  CHECK(path.size() == HCONFIG_MAX_DEPTH);
}

void checkRelations() {
  const HConfigPath units("/units");
  const HConfigPath first("/units/0");
  const HConfigPath name("/units/0/name");
  const HConfigPath other("/other");

  CHECK(first.startsWith(units));
  CHECK(name.startsWith(units));
  CHECK(units.startsWith(units));
  CHECK(!other.startsWith(units));

  CHECK(first.isChildOf(units));
  CHECK(!name.isChildOf(units));   // a grandchild is not a child
  CHECK(name.isChildOf(first));

  CHECK(name.commonPrefixLength(first) == 2);
  CHECK(name.commonPrefixLength(other) == 0);
  CHECK(name.commonPrefixLength(name) == 3);
}

/** @brief The rule the writer's single pass rests on. */
void checkOrdering() {
  const HConfigPath second("/a/2");
  const HConfigPath tenth("/a/10");

  CHECK(HConfigPath::compare(second, tenth) < 0);
  CHECK(HConfigPath::compare(tenth, second) > 0);
  CHECK(HConfigPath::compare(second, second) == 0);

  // An index sorts before a name, which is what makes the ordering total for
  // a file mixing array elements and object members under one parent.
  CHECK(HConfigPath::compare(HConfigPath("/a/0"), HConfigPath("/a/name")) < 0);

  // A prefix sorts before what extends it, so a container header is emitted
  // before the leaves it contains.
  CHECK(HConfigPath::compare(HConfigPath("/a"), HConfigPath("/a/b")) < 0);
  CHECK(HConfigPath::compare(HConfigPath("/a"), HConfigPath("/b")) < 0);
}

void checkToText() {
  const HConfigPath path("/units/0/name");

  char buffer[HCONFIG_MAX_PATH_LEN + 1];
  REQUIRE(path.toText(buffer, sizeof(buffer)));
  CHECK_TEXT(buffer, "/units/0/name");

  char tiny[4];
  CHECK(!path.toText(tiny, sizeof(tiny)));
  CHECK_TEXT(tiny, "");

  char rootText[8];
  REQUIRE(HConfigPath().toText(rootText, sizeof(rootText)));
  CHECK_TEXT(rootText, "");
}

}  // namespace

void runConfigPathTests() noexcept {
  HCoreLibTest::begin("HConfigPath");

  checkParsing();
  checkRejections();
  checkPushAndPop();
  checkRelations();
  checkOrdering();
  checkToText();
}
