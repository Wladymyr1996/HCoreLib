#include "HCoreLibTest.hpp"

#include <cstdio>
#include <cstring>

#include <HOtaImage/HOtaImage.hpp>

/**
 * @file HOtaImageTest.cpp
 * @brief The checks that stand between a device and an image that will not boot.
 *
 * This is the module that decides whether to overwrite a device's only working
 * firmware, and it is deliberately free of ESP-IDF so that decision can be
 * driven here, on a desktop, against bytes written by hand.
 *
 * The golden header below is the price of that freedom: HOtaImage parses the
 * application image layout from its own constants rather than from
 * esp_app_format.h, so something has to hold those constants to the real thing.
 * These are the first bytes of a genuine build - if IDF ever moves a field,
 * this test fails instead of the parser silently reading a version out of the
 * middle of a compile date.
 */

namespace {

/**
 * @brief The first 176 bytes of a real build/HTermo.bin (ESP-IDF v5.5.2, esp32c6).
 *
 * Everything meaningful to the parser is in here: the image header's magic and
 * chip id, the descriptor magic, and the version, project name and IDF version
 * strings. The 112 bytes behind it are the ELF SHA-256 and padding, which
 * nothing in HOtaImage looks at, so the fixture zero-fills them rather than
 * carrying a hundred lines of noise.
 */
const uint8_t kRealHeader[176] = {
    0xe9, 0x07, 0x02, 0x30, 0xf8, 0x03, 0x80, 0x40, 0xee, 0x00, 0x00, 0x00,
    0x0d, 0x00, 0x00, 0x00, 0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x20, 0x00, 0x0e, 0x42, 0x8c, 0xd3, 0x02, 0x00, 0x32, 0x54, 0xcd, 0xab,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x30, 0x2e, 0x31, 0x2e, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x54, 0x65, 0x72,
    0x6d, 0x6f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x30, 0x31, 0x3a, 0x31, 0x38, 0x3a, 0x30, 0x38,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 0x75, 0x67, 0x20,
    0x32, 0x33, 0x20, 0x32, 0x30, 0x32, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x76, 0x35, 0x2e, 0x35, 0x2e, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/** @brief One image header, editable field by field. */
struct Fixture {
  uint8_t bytes[HOTAIMAGE_HEADER_BYTES];

  Fixture() noexcept {
    std::memset(bytes, 0, sizeof(bytes));
    std::memcpy(bytes, kRealHeader, sizeof(kRealHeader));
  }

  /** @brief Overwrites one of the descriptor's fixed 32-byte string fields. */
  void setField(size_t offsetInDescriptor, const char* text) noexcept {
    uint8_t* field = bytes + HOTAIMAGE_DESC_OFFSET + offsetInDescriptor;
    std::memset(field, 0, HOTAIMAGE_STRING_LEN);
    std::memcpy(field, text, std::strlen(text));
  }

  void setVersion(const char* text) noexcept { setField(16, text); }
  void setProject(const char* text) noexcept { setField(48, text); }
};

/** @brief The policy this device actually ships with. */
HOtaPolicy defaultPolicy(const char* running) noexcept {
  HOtaPolicy policy = {};
  policy.expectedProject = "HTermo";
  policy.runningVersion = running;
  policy.expectedChipId = HOTAIMAGE_CHIP_ESP32C6;
  policy.partitionSize = 0x370000;
  policy.allowSameVersion = true;
  policy.allowDowngrade = false;
  return policy;
}

/**
 * @brief The parser against a genuine image, which is what pins the offsets.
 *
 * If any of these three strings comes back wrong, a field moved and every
 * comparison built on it is reading the wrong bytes.
 */
void checkRealImage() {
  Fixture image;
  HOtaImageInfo info = {};

  CHECK(HOtaImage::parse(image.bytes, sizeof(image.bytes), info) == HOtaVerdict::Ok);
  CHECK_TEXT(info.projectName, "HTermo");
  CHECK_TEXT(info.version, "0.1.0");
  CHECK_TEXT(info.idfVersion, "v5.5.2");
  CHECK(info.chipId == HOTAIMAGE_CHIP_ESP32C6);
}

void checkParseFailures() {
  Fixture image;
  HOtaImageInfo info = {};

  // One byte short of a full header is Incomplete, not a guess. A streaming
  // caller must buffer the whole header before it commits anything to flash.
  CHECK(HOtaImage::parse(image.bytes, HOTAIMAGE_HEADER_BYTES - 1, info) ==
        HOtaVerdict::Incomplete);
  CHECK(HOtaImage::parse(nullptr, HOTAIMAGE_HEADER_BYTES, info) == HOtaVerdict::Incomplete);

  // A .txt, a .zip, an HTML error page a proxy served instead of the firmware -
  // all of them fail here, on byte 0, before anything is written.
  Fixture notAnImage;
  notAnImage.bytes[0] = 0x7F;
  CHECK(HOtaImage::parse(notAnImage.bytes, sizeof(notAnImage.bytes), info) ==
        HOtaVerdict::NotAnImage);

  // A valid image built without an application descriptor: the bootloader would
  // run it, and nothing could say what version it was.
  Fixture noDescriptor;
  noDescriptor.bytes[HOTAIMAGE_DESC_OFFSET] = 0x00;
  CHECK(HOtaImage::parse(noDescriptor.bytes, sizeof(noDescriptor.bytes), info) ==
        HOtaVerdict::NoDescriptor);
}

/**
 * @brief A 32-byte field filled edge to edge, which carries no terminator.
 *
 * The case that reads off the end of the structure if the parser trusts the
 * descriptor's strings to be terminated. Nothing in the format promises that.
 */
void checkUnterminatedField() {
  Fixture image;
  uint8_t* project = image.bytes + HOTAIMAGE_DESC_OFFSET + 48;
  std::memset(project, 'A', HOTAIMAGE_STRING_LEN);

  HOtaImageInfo info = {};
  REQUIRE(HOtaImage::parse(image.bytes, sizeof(image.bytes), info) == HOtaVerdict::Ok);
  CHECK(std::strlen(info.projectName) == HOTAIMAGE_STRING_LEN);
  CHECK_TEXT(info.projectName, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
}

void checkVersionParsing() {
  uint32_t major = 0, minor = 0, patch = 0;

  REQUIRE(HOtaImage::parseVersion("1.2.3", major, minor, patch));
  CHECK(major == 1 && minor == 2 && patch == 3);

  // A missing component is zero, so "1.2" orders as 1.2.0.
  REQUIRE(HOtaImage::parseVersion("1.2", major, minor, patch));
  CHECK(major == 1 && minor == 2 && patch == 0);

  REQUIRE(HOtaImage::parseVersion("7", major, minor, patch));
  CHECK(major == 7 && minor == 0 && patch == 0);

  // A pre-release suffix is ignored for ordering rather than ranked.
  REQUIRE(HOtaImage::parseVersion("1.2.3-rc1", major, minor, patch));
  CHECK(major == 1 && minor == 2 && patch == 3);

  REQUIRE(HOtaImage::parseVersion("2.0.0+build7", major, minor, patch));
  CHECK(major == 2 && minor == 0 && patch == 0);

  // Anything that does not START with a digit is unorderable. Without that
  // rule "v1.2.3" would parse as 0.0.0 and compare equal to every other
  // unnumbered string - including the one already running.
  CHECK(!HOtaImage::parseVersion("v1.2.3", major, minor, patch));
  CHECK(!HOtaImage::parseVersion("", major, minor, patch));
  CHECK(!HOtaImage::parseVersion(nullptr, major, minor, patch));
  CHECK(!HOtaImage::parseVersion("dev", major, minor, patch));
  CHECK(!HOtaImage::parseVersion("1.x.3", major, minor, patch));

  // Saturating rather than wrapping matters in one direction only: a wrapped
  // component would compare SMALL, which is the one outcome that could let a
  // downgrade through.
  REQUIRE(HOtaImage::parseVersion("99999999999.0.0", major, minor, patch));
  CHECK(major > 100000);
}

void checkVersionOrdering() {
  int order = 0;

  REQUIRE(HOtaImage::compareVersions("1.0.0", "1.0.0", order));
  CHECK(order == 0);

  REQUIRE(HOtaImage::compareVersions("1.0.1", "1.0.0", order));
  CHECK(order == 1);

  REQUIRE(HOtaImage::compareVersions("1.0.0", "1.0.1", order));
  CHECK(order == -1);

  // Component by component, not lexicographically: "0.9.0" is older than
  // "0.10.0" even though it sorts after it as text.
  REQUIRE(HOtaImage::compareVersions("0.10.0", "0.9.0", order));
  CHECK(order == 1);

  REQUIRE(HOtaImage::compareVersions("2.0.0", "1.99.99", order));
  CHECK(order == 1);

  // "1.2" and "1.2.0" are the same version written two ways.
  REQUIRE(HOtaImage::compareVersions("1.2", "1.2.0", order));
  CHECK(order == 0);

  // An unparseable side is refused rather than treated as equal, and the
  // caller's variable is left alone so a stale value cannot be read as a verdict.
  order = 99;
  CHECK(!HOtaImage::compareVersions("dev", "1.0.0", order));
  CHECK(order == 99);
  CHECK(!HOtaImage::compareVersions("1.0.0", "", order));
  CHECK(order == 99);
}

void checkPolicy() {
  HOtaImageInfo info = {};
  std::snprintf(info.projectName, sizeof(info.projectName), "HTermo");
  std::snprintf(info.version, sizeof(info.version), "1.0.0");
  info.chipId = HOTAIMAGE_CHIP_ESP32C6;

  CHECK(HOtaImage::judge(info, 0x1000, defaultPolicy("0.9.0")) == HOtaVerdict::Ok);

  // The chip is checked FIRST and before anything else can excuse it: an image
  // for another target passes every string check and then does not boot.
  HOtaImageInfo wrongChip = info;
  wrongChip.chipId = 0x0005;
  CHECK(HOtaImage::judge(wrongChip, 0x1000, defaultPolicy("0.9.0")) == HOtaVerdict::WrongChip);

  // Another Hatynka product's firmware: same chip, same bootloader, same
  // descriptor format, and nothing above this catches it.
  HOtaImageInfo wrongProject = info;
  std::snprintf(wrongProject.projectName, sizeof(wrongProject.projectName), "HDoor");
  CHECK(HOtaImage::judge(wrongProject, 0x1000, defaultPolicy("0.9.0")) ==
        HOtaVerdict::WrongProject);

  CHECK(HOtaImage::judge(info, 0x400000, defaultPolicy("0.9.0")) == HOtaVerdict::TooLarge);

  // 0 means the total is not known - a chunked upload with no Content-Length.
  // Skipped rather than guessed at, or good images would be refused.
  CHECK(HOtaImage::judge(info, 0, defaultPolicy("0.9.0")) == HOtaVerdict::Ok);
}

void checkDowngradePolicy() {
  HOtaImageInfo info = {};
  std::snprintf(info.projectName, sizeof(info.projectName), "HTermo");
  std::snprintf(info.version, sizeof(info.version), "1.0.0");
  info.chipId = HOTAIMAGE_CHIP_ESP32C6;

  // The requirement this whole module exists for.
  CHECK(HOtaImage::judge(info, 0, defaultPolicy("1.1.0")) == HOtaVerdict::OlderVersion);

  HOtaPolicy forced = defaultPolicy("1.1.0");
  forced.allowDowngrade = true;
  CHECK(HOtaImage::judge(info, 0, forced) == HOtaVerdict::Ok);

  // Re-flashing the running version is allowed by default: a device that
  // refuses it cannot be repaired in the field after a partial write.
  CHECK(HOtaImage::judge(info, 0, defaultPolicy("1.0.0")) == HOtaVerdict::Ok);

  HOtaPolicy strict = defaultPolicy("1.0.0");
  strict.allowSameVersion = false;
  CHECK(HOtaImage::judge(info, 0, strict) == HOtaVerdict::SameVersion);

  // An image whose version cannot be ordered is refused even though nothing is
  // demonstrably wrong with it. It cannot be SHOWN to be an upgrade, and this
  // comparison is the whole of what stops a downgrade.
  HOtaImageInfo unversioned = info;
  std::snprintf(unversioned.version, sizeof(unversioned.version), "dev");
  CHECK(HOtaImage::judge(unversioned, 0, defaultPolicy("1.0.0")) == HOtaVerdict::BadVersion);

  // And a device running an unorderable version cannot be updated either,
  // rather than accepting anything at all.
  CHECK(HOtaImage::judge(info, 0, defaultPolicy("dev")) == HOtaVerdict::BadVersion);
}

/** @brief The whole path, the way both OTA callers use it. */
void checkInspect() {
  Fixture image;
  HOtaImageInfo info = {};

  image.setVersion("2.0.0");
  CHECK(HOtaImage::inspect(image.bytes, sizeof(image.bytes), 0x2000,
                           defaultPolicy("1.0.0"), info) == HOtaVerdict::Ok);
  CHECK_TEXT(info.version, "2.0.0");

  image.setVersion("0.0.9");
  CHECK(HOtaImage::inspect(image.bytes, sizeof(image.bytes), 0x2000,
                           defaultPolicy("1.0.0"), info) == HOtaVerdict::OlderVersion);

  // Even a refusal fills in what the image said, so a UI can report "you
  // offered 0.0.9, this device runs 1.0.0" rather than a bare failure.
  CHECK_TEXT(info.version, "0.0.9");

  image.setProject("HDoor");
  CHECK(HOtaImage::inspect(image.bytes, sizeof(image.bytes), 0x2000,
                           defaultPolicy("1.0.0"), info) == HOtaVerdict::WrongProject);
}

void checkVerdictText() {
  // Never null, for every value - these go straight into a log line and a REST
  // body, and a null here would be a crash in the error path.
  CHECK(HOtaImage::verdictText(HOtaVerdict::Ok) != nullptr);
  CHECK_TEXT(HOtaImage::verdictText(HOtaVerdict::OlderVersion), "older version");
  CHECK_TEXT(HOtaImage::verdictText(HOtaVerdict::WrongProject), "wrong device");
  CHECK_TEXT(HOtaImage::verdictText(HOtaVerdict::WrongChip), "wrong chip");
  CHECK_TEXT(HOtaImage::verdictText(static_cast<HOtaVerdict>(200)), "unknown");
}

}  // namespace

void runOtaImageTests() noexcept {
  HCoreLibTest::begin("HOtaImage");

  checkRealImage();
  checkParseFailures();
  checkUnterminatedField();
  checkVersionParsing();
  checkVersionOrdering();
  checkPolicy();
  checkDowngradePolicy();
  checkInspect();
  checkVerdictText();
}
