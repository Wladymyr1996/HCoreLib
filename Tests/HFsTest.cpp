#include "HCoreLibTest.hpp"

#include <cstring>

#include <HFs/HFs.hpp>

/**
 * @file HFsTest.cpp
 * @brief The desktop backend against the contract both backends implement.
 *
 * Only one backend is ever compiled in, so this cannot compare the two - what
 * it can do is hold the desktop one to the interface's documented answers, so
 * that code written and tested here behaves the same way when LittleFS is
 * underneath it. The rename-replaces-the-target rule is the one that matters
 * most: HConfigWriter's crash safety is built entirely on it.
 */

namespace {

constexpr const char* kDir = "fstest";
constexpr const char* kFile = "fstest/file.txt";

/** @brief Names seen by the last listDir(), in enumeration order. */
char gSeen[8][32];
size_t gSeenCount = 0;
size_t gStopAfter = 0;

bool onEntry(const char* name, bool isDirectory) noexcept {
  (void)isDirectory;
  if (gSeenCount < 8) {
    std::snprintf(gSeen[gSeenCount], sizeof(gSeen[0]), "%s", name);
    ++gSeenCount;
  }
  return gStopAfter == 0 || gSeenCount < gStopAfter;
}

/** @brief True if listDir() reported `name`, whatever order it came in. */
bool sawEntry(const char* name) {
  for (size_t i = 0; i < gSeenCount; ++i) {
    if (std::strcmp(gSeen[i], name) == 0) {
      return true;
    }
  }
  return false;
}

void removeTree() {
  HFs::HFileSystem::deleteFile(kFile);
  HFs::HFileSystem::deleteFile("fstest/other.txt");
  HFs::HFileSystem::deleteFile("fstest/renamed.txt");
  HFs::HFileSystem::removeDir("fstest/sub");
  HFs::HFileSystem::removeDir(kDir);
}

void checkDirectories() {
  removeTree();

  CHECK(!HFs::HFileSystem::exists(kDir));
  CHECK(HFs::HFileSystem::createDir(kDir));
  CHECK(HFs::HFileSystem::exists(kDir));
  CHECK(HFs::HFileSystem::isDirectory(kDir));

  // Idempotent: an existing directory is success, not a conflict. HConfig
  // calls this on every init().
  CHECK(HFs::HFileSystem::createDir(kDir));

  CHECK(HFs::HFileSystem::createDir("fstest/sub"));
  CHECK(HFs::HFileSystem::removeDir("fstest/sub"));
  CHECK(!HFs::HFileSystem::exists("fstest/sub"));
  CHECK(!HFs::HFileSystem::removeDir("fstest/sub"));  // nothing there to remove
}

void checkFileIo() {
  REQUIRE(HFs::HFileSystem::createDir(kDir));

  {
    HFs::HFile file;
    REQUIRE(HFs::HFileSystem::openFile(kFile, file, "wb"));
    CHECK(file.isOpen());
    CHECK(file.write("hello world", 11) == 11);
    file.close();
    CHECK(!file.isOpen());
    file.close();  // closing twice is safe
  }

  CHECK(HFs::HFileSystem::exists(kFile));
  CHECK(!HFs::HFileSystem::isDirectory(kFile));

  {
    HFs::HFile file;
    REQUIRE(HFs::HFileSystem::openFile(kFile, file, "rb"));
    CHECK(file.size() == 11);

    char buffer[16] = {};
    CHECK(file.read(buffer, 5) == 5);
    CHECK(std::strncmp(buffer, "hello", 5) == 0);

    // size() must not disturb the position - HConfigParser asks mid-stream.
    CHECK(file.size() == 11);

    std::memset(buffer, 0, sizeof(buffer));
    CHECK(file.read(buffer, sizeof(buffer)) == 6);
    CHECK(std::strncmp(buffer, " world", 6) == 0);

    // Reading at the end returns nothing rather than failing.
    CHECK(file.read(buffer, sizeof(buffer)) == 0);

    REQUIRE(file.seek(6));
    std::memset(buffer, 0, sizeof(buffer));
    CHECK(file.read(buffer, 5) == 5);
    CHECK(std::strncmp(buffer, "world", 5) == 0);
    file.close();
  }

  // A handle that was never opened answers rather than crashing.
  HFs::HFile unopened;
  char scratch[4];
  CHECK(!unopened.isOpen());
  CHECK(unopened.read(scratch, sizeof(scratch)) == 0);
  CHECK(unopened.write(scratch, sizeof(scratch)) == 0);
  CHECK(unopened.size() == 0);

  // createFile truncates whatever was there.
  CHECK(HFs::HFileSystem::createFile(kFile));
  HFs::HFile emptied;
  REQUIRE(HFs::HFileSystem::openFile(kFile, emptied, "rb"));
  CHECK(emptied.size() == 0);
  emptied.close();
}

void checkMissingAndWrongTypes() {
  HFs::HFile file;
  CHECK(!HFs::HFileSystem::openFile("fstest/nope.txt", file, "rb"));
  CHECK(!file.isOpen());

  CHECK(!HFs::HFileSystem::deleteFile("fstest/nope.txt"));
  CHECK(!HFs::HFileSystem::deleteFile(kDir));  // a directory is not a file
  CHECK(HFs::HFileSystem::exists(kDir));
}

/** @brief The atomic swap HConfigWriter's crash safety is built on. */
void checkRenameReplaces() {
  REQUIRE(HCoreLibTest::writeTextFile(kFile, "new"));
  REQUIRE(HCoreLibTest::writeTextFile("fstest/renamed.txt", "old"));

  REQUIRE(HFs::HFileSystem::rename(kFile, "fstest/renamed.txt"));
  CHECK(!HFs::HFileSystem::exists(kFile));

  char text[16];
  REQUIRE(HCoreLibTest::readTextFile("fstest/renamed.txt", text, sizeof(text)) > 0);
  CHECK_TEXT(text, "new");

  CHECK(!HFs::HFileSystem::rename("fstest/nope.txt", "fstest/whatever.txt"));
}

void checkListDir() {
  removeTree();
  REQUIRE(HFs::HFileSystem::createDir(kDir));
  REQUIRE(HCoreLibTest::writeTextFile(kFile, "a"));
  REQUIRE(HCoreLibTest::writeTextFile("fstest/other.txt", "b"));
  REQUIRE(HFs::HFileSystem::createDir("fstest/sub"));

  gSeenCount = 0;
  gStopAfter = 0;
  CHECK(HFs::HFileSystem::listDir(kDir, HFsEntryVisitor::create<&onEntry>()));
  CHECK(gSeenCount == 3);
  CHECK(sawEntry("file.txt"));
  CHECK(sawEntry("other.txt"));
  CHECK(sawEntry("sub"));

  // Returning false stops the walk - the reason the visitor returns anything.
  gSeenCount = 0;
  gStopAfter = 1;
  CHECK(HFs::HFileSystem::listDir(kDir, HFsEntryVisitor::create<&onEntry>()));
  CHECK(gSeenCount == 1);

  gStopAfter = 0;
  CHECK(!HFs::HFileSystem::listDir("fstest/nope", HFsEntryVisitor::create<&onEntry>()));

  // A non-empty directory cannot be removed, which is what keeps a stray
  // removeDir() from taking a configuration with it.
  CHECK(!HFs::HFileSystem::removeDir(kDir));
}

}  // namespace

void runFsTests() noexcept {
  HCoreLibTest::begin("HFs");

  checkDirectories();
  checkFileIo();
  checkMissingAndWrongTypes();
  checkRenameReplaces();
  checkListDir();

  removeTree();
}
