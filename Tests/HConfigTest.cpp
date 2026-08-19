#include "HCoreLibTest.hpp"

#include <cstring>

#include <etl/span.h>

#include <HConfig/HConfig.hpp>
#include <HConfig/HConfigWriter.hpp>
#include <HFs/HFs.hpp>

/**
 * @file HConfigTest.cpp
 * @brief HConfig against a real filesystem, including the bytes on disk.
 *
 * The exact-text checks here are the point of the file. A config file written
 * by a firmware in the field has to be readable by the firmware that replaces
 * it, so the format is a compatibility contract and not an implementation
 * detail - which makes "the writer still emits exactly this" a regression test
 * rather than a tautology.
 *
 * Everything runs against the desktop backend, which is plain files in the
 * process's working directory. That is the same HConfig, HConfigParser and
 * HConfigWriter the device runs; only HIFs underneath differs.
 */

namespace {

/** @brief The file two of the checks below are written against. */
constexpr const char* kExpectedFile =
    "name[s]: Garage\n"
    "travelMs[i]: 4080\n"
    "units[ao]:\n"
    "  [o]:\n"
    "    id[i]: 1\n"
    "    label[s]: left\n"
    "  [o]:\n"
    "    id[i]: 2\n"
    "    label[s]: right\n";

/** @brief Writes the fixture above through the public API. */
bool writeFixture() {
  const HConfigEntry entries[] = {
      {"/name", HValue("Garage")},
      {"/travelMs", HValue(4080)},
      {"/units/0/id", HValue(1)},
      {"/units/0/label", HValue("left")},
      {"/units/1/id", HValue(2)},
      {"/units/1/label", HValue("right")},
  };
  return HConfig::write("door1", etl::span<const HConfigEntry>(entries));
}

void checkWrittenFormat() {
  HCoreLibTest::clearConfigDir();
  REQUIRE(writeFixture());

  char text[512];
  REQUIRE(HCoreLibTest::readTextFile("config/door1.cfg", text, sizeof(text)) > 0);
  CHECK_TEXT(text, kExpectedFile);
}

void checkReading() {
  HCoreLibTest::clearConfigDir();
  REQUIRE(writeFixture());

  CHECK(HConfig::read("door1", "/travelMs", HValue(0)).asInt() == 4080);
  // The leading slash is optional, here as everywhere else.
  CHECK(HConfig::read("door1", "travelMs", HValue(0)).asInt() == 4080);
  const HValue label = HConfig::read("door1", "/units/1/label", HValue(""));
  CHECK(std::strcmp(label.asString().c_str(), "right") == 0);

  // The FILE decides the type, not the default. A [s] line read with an Int
  // default comes back a String.
  const HValue name = HConfig::read("door1", "/name", HValue(0));
  CHECK(name.isString());

  // Absent file, absent path and a container path all yield the default. The
  // container is not an error - it simply holds no value.
  CHECK(HConfig::read("nosuch", "/x", HValue(7)).asInt() == 7);
  CHECK(HConfig::read("door1", "/nosuch", HValue(7)).asInt() == 7);
  CHECK(HConfig::read("door1", "/units", HValue(7)).asInt() == 7);
  CHECK(HConfig::read("door1", "/units/0", HValue(7)).asInt() == 7);

  CHECK(HConfig::has("door1", "/travelMs"));
  CHECK(HConfig::has("door1", "/units/0/id"));
  CHECK(!HConfig::has("door1", "/units"));      // a container is not a value
  CHECK(!HConfig::has("door1", "/nope"));
  CHECK(!HConfig::has("nosuch", "/travelMs"));

  // count() is what makes an array iterable without handing out a sub-tree.
  CHECK(HConfig::count("door1", "/units") == 2);
  CHECK(HConfig::count("door1", "/units/0") == 2);
  CHECK(HConfig::count("door1", "") == 3);
  CHECK(HConfig::count("door1", "/travelMs") == 0);
  CHECK(HConfig::count("nosuch", "") == 0);
}

/** @brief One pass over the file for many paths - the answer to per-read opens. */
void checkReadMany() {
  HCoreLibTest::clearConfigDir();
  REQUIRE(writeFixture());

  const HConfigEntry wanted[] = {
      {"/travelMs", HValue(0)},
      {"/units/0/label", HValue("")},
      {"/absent", HValue(0)},
  };
  HValue out[] = {HValue(-1), HValue("untouched"), HValue(99)};

  const size_t found = HConfig::readMany("door1", etl::span<const HConfigEntry>(wanted),
                                         etl::span<HValue>(out));
  CHECK(found == 2);
  CHECK(out[0].asInt() == 4080);
  CHECK(std::strcmp(out[1].asString().c_str(), "left") == 0);
  CHECK(out[2].asInt() == 99);  // absent paths leave the seeded default alone

  // A short output span is refused rather than written past.
  HValue tooFew[] = {HValue(0)};
  CHECK(HConfig::readMany("door1", etl::span<const HConfigEntry>(wanted),
                          etl::span<HValue>(tooFew)) == 0);
}

/** @brief sort() must produce exactly the order write() validates against. */
void checkSort() {
  HConfigEntry entries[] = {
      {"/units/10/id", HValue(10)},
      {"/name", HValue("x")},
      {"/units/2/id", HValue(2)},
  };
  HConfig::sort(etl::span<HConfigEntry>(entries));

  CHECK(std::strcmp(entries[0].path.c_str(), "/name") == 0);
  CHECK(std::strcmp(entries[1].path.c_str(), "/units/2/id") == 0);
  CHECK(std::strcmp(entries[2].path.c_str(), "/units/10/id") == 0);

  // And the values travelled WITH their types - the trap HConfigEntry's
  // hand-written operator= exists for. A member-wise assignment would have
  // coerced the String into the Int slot it was shifted over.
  CHECK(entries[0].value.isString());
  CHECK(entries[1].value.isInt());
}

/**
 * @brief Every rejection leaves the existing file byte-identical.
 *
 * All-or-nothing is the contract, and the only way to check it is to look at
 * the file afterwards rather than at the return value alone.
 */
void checkWriteRejections() {
  HCoreLibTest::clearConfigDir();
  REQUIRE(writeFixture());

  const HConfigEntry unsorted[] = {
      {"/travelMs", HValue(1)},
      {"/name", HValue("x")},
  };
  CHECK(!HConfig::write("door1", etl::span<const HConfigEntry>(unsorted)));

  const HConfigEntry duplicated[] = {
      {"/name", HValue("x")},
      {"/name", HValue("y")},
  };
  CHECK(!HConfig::write("door1", etl::span<const HConfigEntry>(duplicated)));

  // Sparse indices cannot be represented: an array has no holes.
  const HConfigEntry sparse[] = {
      {"/units/0/id", HValue(1)},
      {"/units/2/id", HValue(2)},
  };
  CHECK(!HConfig::write("door1", etl::span<const HConfigEntry>(sparse)));

  const HConfigEntry badPath[] = {{"/a//b", HValue(1)}};
  CHECK(!HConfig::write("door1", etl::span<const HConfigEntry>(badPath)));

  char text[512];
  REQUIRE(HCoreLibTest::readTextFile("config/door1.cfg", text, sizeof(text)) > 0);
  CHECK_TEXT(text, kExpectedFile);
}

/** @brief A Null has no tag, so it has no line - and leaves no empty container. */
void checkNullEntriesAreSkipped() {
  HCoreLibTest::clearConfigDir();

  const HConfigEntry entries[] = {
      {"/kept", HValue(1)},
      {"/skipped", HValue()},
  };
  REQUIRE(HConfig::write("nulls", etl::span<const HConfigEntry>(entries)));

  char text[128];
  REQUIRE(HCoreLibTest::readTextFile("config/nulls.cfg", text, sizeof(text)) > 0);
  CHECK_TEXT(text, "kept[i]: 1\n");
}

/**
 * @brief patch() copies through everything it is not changing, byte for byte.
 *
 * Comments and blank lines survive because a config file is something a person
 * may have edited by hand, and a patch that reformatted it would throw that
 * away silently.
 */
void checkPatchPreservesTheRest() {
  HCoreLibTest::clearConfigDir();

  REQUIRE(HFs::HFileSystem::createDir("config"));
  REQUIRE(HCoreLibTest::writeTextFile("config/hand.cfg",
                                      "# hand-written, and it should stay that way\n"
                                      "name[s]: Garage\n"
                                      "\n"
                                      "travelMs[i]: 4080\n"));

  const HConfigEntry changes[] = {{"travelMs", HValue(5000)}};
  REQUIRE(HConfig::patch("hand", etl::span<const HConfigEntry>(changes)));

  char text[256];
  REQUIRE(HCoreLibTest::readTextFile("config/hand.cfg", text, sizeof(text)) > 0);
  CHECK_TEXT(text,
             "# hand-written, and it should stay that way\n"
             "name[s]: Garage\n"
             "\n"
             "travelMs[i]: 5000\n");

  // The tag the file declares wins: patching an [i] line with a string
  // coerces through the Int rather than retyping the setting.
  const HConfigEntry retype[] = {{"travelMs", HValue("77")}};
  REQUIRE(HConfig::patch("hand", etl::span<const HConfigEntry>(retype)));
  CHECK(HConfig::read("hand", "travelMs", HValue(0)).isInt());
  CHECK(HConfig::read("hand", "travelMs", HValue(0)).asInt() == 77);
}

/** @brief A new top-level key is appended; a new nested one cannot be. */
void checkPatchAppendRules() {
  HCoreLibTest::clearConfigDir();
  REQUIRE(HFs::HFileSystem::createDir("config"));
  REQUIRE(HCoreLibTest::writeTextFile("config/append.cfg", "name[s]: Garage\n"));

  const HConfigEntry appended[] = {{"lang", HValue("en")}};
  REQUIRE(HConfig::patch("append", etl::span<const HConfigEntry>(appended)));

  char text[256];
  REQUIRE(HCoreLibTest::readTextFile("config/append.cfg", text, sizeof(text)) > 0);
  CHECK_TEXT(text,
             "name[s]: Garage\n"
             "lang[s]: en\n");

  // A deeper path that is not there would have to be inserted at a position
  // the one-pass filter has already streamed past. It fails instead, leaving
  // the file untouched, so a caller cannot half-apply a change.
  const HConfigEntry nested[] = {{"/units/0/id", HValue(1)}};
  CHECK(!HConfig::patch("append", etl::span<const HConfigEntry>(nested)));

  char after[256];
  REQUIRE(HCoreLibTest::readTextFile("config/append.cfg", after, sizeof(after)) > 0);
  CHECK_TEXT(after,
             "name[s]: Garage\n"
             "lang[s]: en\n");
}

/** @brief One bad hand edit costs that line, not the rest of the file. */
void checkMalformedLinesAreSurvivable() {
  HCoreLibTest::clearConfigDir();
  REQUIRE(HFs::HFileSystem::createDir("config"));
  REQUIRE(HCoreLibTest::writeTextFile("config/messy.cfg",
                                      "good[i]: 1\n"
                                      "this line is not a setting at all\n"
                                      "alsoGood[s]: yes\n"));

  CHECK(HConfig::read("messy", "good", HValue(0)).asInt() == 1);
  CHECK(std::strcmp(HConfig::read("messy", "alsoGood", HValue("")).asString().c_str(),
                    "yes") == 0);
}

/**
 * @brief init() discards a staging file left behind by an interrupted write.
 *
 * A leftover means power was lost between building the new file and swapping
 * it in - so the live file was never touched and the orphan is by definition
 * incomplete. Discarding it is the whole of the recovery.
 */
void checkStagingRecovery() {
  HCoreLibTest::clearConfigDir();
  REQUIRE(writeFixture());

  REQUIRE(HCoreLibTest::writeTextFile(HConfigWriter::stagingPath(), "half a fi"));
  REQUIRE(HFs::HFileSystem::exists(HConfigWriter::stagingPath()));

  HConfig::init();
  CHECK(!HFs::HFileSystem::exists(HConfigWriter::stagingPath()));

  // The live file is what it always was.
  char text[512];
  REQUIRE(HCoreLibTest::readTextFile("config/door1.cfg", text, sizeof(text)) > 0);
  CHECK_TEXT(text, kExpectedFile);
}

void checkRemoval() {
  HCoreLibTest::clearConfigDir();
  REQUIRE(writeFixture());

  const HConfigEntry other[] = {{"/x", HValue(1)}};
  REQUIRE(HConfig::write("second", etl::span<const HConfigEntry>(other)));

  CHECK(HConfig::remove("door1"));
  CHECK(!HFs::HFileSystem::exists("config/door1.cfg"));
  CHECK(HFs::HFileSystem::exists("config/second.cfg"));

  // Idempotent: already absent is the state that was asked for. A module name
  // that cannot become a path is a different matter and fails.
  CHECK(HConfig::remove("door1"));
  CHECK(!HConfig::remove(""));
  CHECK(!HConfig::remove(nullptr));

  CHECK(HConfig::removeAll());
  CHECK(!HFs::HFileSystem::exists("config/second.cfg"));
  CHECK(HFs::HFileSystem::isDirectory("config"));  // the directory itself stays
  CHECK(HConfig::removeAll());                     // already empty is success
}

}  // namespace

void runConfigTests() noexcept {
  HCoreLibTest::begin("HConfig");

  HConfig::init();

  checkWrittenFormat();
  checkReading();
  checkReadMany();
  checkSort();
  checkWriteRejections();
  checkNullEntriesAreSkipped();
  checkPatchPreservesTheRest();
  checkPatchAppendRules();
  checkMalformedLinesAreSurvivable();
  checkStagingRecovery();
  checkRemoval();

  HCoreLibTest::clearConfigDir();
}
