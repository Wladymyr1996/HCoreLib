#include "HCoreLibTest.hpp"

#include <cstring>

#include <HAuth/HAuth.hpp>
#include <HConfig/HConfig.hpp>
#include <HFs/HFs.hpp>
#include <HSha256/HSha256.hpp>
#include <HValue/HValue.hpp>

/**
 * @file HAuthTest.cpp
 * @brief The password, the file it is not in, and the single session key.
 *
 * Two properties are worth a regression test above the rest. A fresh device is
 * deliberately OPEN, which is a decision that looks like a bug to anyone
 * reading it later and must not be "fixed" by accident - a device that shipped
 * locked would be a device nobody could configure. And what lands in
 * `config/auth.cfg` must be the hash and never the password, which is checked
 * here by looking at the actual bytes on disk.
 */

/**
 * The hash goes to disk THROUGH an HValue, so the payload has to hold all 64
 * hexadecimal characters of it. Below that it is truncated on the way out and
 * the correct password stops verifying after the next reboot - a failure that
 * shows up on a device in a cupboard rather than here, which is exactly why it
 * is worth a compile error. See Tests/Config/HCoreLibConfig.h.
 */
static_assert(HVALUE_MAX_STRING_LEN >= HSHA256_HEX_BYTES - 1,
              "HVALUE_MAX_STRING_LEN is too small to store a SHA-256 hex digest, "
              "so HAuth's stored password hash would be truncated");

namespace {

constexpr const char* kPassword = "correct horse";
constexpr const char* kWrong = "battery staple";

void checkFreshDeviceIsOpen() {
  HCoreLibTest::clearConfigDir();
  HConfig::init();
  HAuth::init();

  CHECK(!HAuth::isPasswordSet());

  // Any password verifies while none is set. Deliberate, and logged as a
  // warning every time - a state to leave rather than to live in.
  CHECK(HAuth::verify(kPassword));
  CHECK(HAuth::verify(""));
  CHECK(HAuth::verify(nullptr));
}

void checkSetAndVerify() {
  HCoreLibTest::clearConfigDir();
  HConfig::init();
  HAuth::init();

  CHECK(!HAuth::setPassword(""));
  CHECK(!HAuth::setPassword(nullptr));
  CHECK(!HAuth::isPasswordSet());

  REQUIRE(HAuth::setPassword(kPassword));
  CHECK(HAuth::isPasswordSet());
  CHECK(HAuth::verify(kPassword));
  CHECK(!HAuth::verify(kWrong));
  CHECK(!HAuth::verify(""));
  CHECK(!HAuth::verify(nullptr));

  // Replacing it takes effect immediately, old password included.
  REQUIRE(HAuth::setPassword(kWrong));
  CHECK(HAuth::verify(kWrong));
  CHECK(!HAuth::verify(kPassword));
}

/**
 * @brief What is on disk is the hash.
 *
 * Reading the file must not hand over the secret.
 */
void checkStoredAsHash() {
  HCoreLibTest::clearConfigDir();
  HConfig::init();
  HAuth::init();
  REQUIRE(HAuth::setPassword(kPassword));

  char text[512];
  REQUIRE(HCoreLibTest::readTextFile("config/auth.cfg", text, sizeof(text)) > 0);

  char expectedHex[HSHA256_HEX_BYTES];
  REQUIRE(HSha256::hashToHex(kPassword, expectedHex, sizeof(expectedHex)));

  CHECK(std::strstr(text, expectedHex) != nullptr);
  CHECK(std::strstr(text, kPassword) == nullptr);
}

/** @brief The hash survives a restart, which is the only reason to store it. */
void checkPersistence() {
  HCoreLibTest::clearConfigDir();
  HConfig::init();
  HAuth::init();
  REQUIRE(HAuth::setPassword(kPassword));

  // init() again is what a reboot looks like from this class's point of view.
  HAuth::init();
  CHECK(HAuth::isPasswordSet());
  CHECK(HAuth::verify(kPassword));
  CHECK(!HAuth::verify(kWrong));

  // And a factory reset really does open the device up again.
  HCoreLibTest::clearConfigDir();
  HAuth::init();
  CHECK(!HAuth::isPasswordSet());
}

/**
 * @brief A hash truncated by an older build locks the device, not opens it.
 *
 * The static_assert in HAuth.cpp makes this impossible to WRITE now, but a
 * device already in the field may be carrying one, and there is a wrong way to
 * handle that: treating an unusable hash as "no password" would take a device
 * that thinks it is locked and open its API to anyone. It stays locked, and
 * init() says why in the log.
 */
void checkTruncatedStoredHash() {
  HCoreLibTest::clearConfigDir();
  HConfig::init();

  // 31 hexadecimal characters - exactly what the library's default HValue
  // payload used to leave behind.
  REQUIRE(HFs::HFileSystem::createDir("config"));
  REQUIRE(HCoreLibTest::writeTextFile("config/auth.cfg",
                                      "hash[s]: 8f4e3c2b1a0987654321fedcba09876\n"));
  HAuth::init();

  CHECK(HAuth::isPasswordSet());
  CHECK(!HAuth::verify(kPassword));
  CHECK(!HAuth::verify(""));

  // And setting a password again is the way out of it.
  REQUIRE(HAuth::setPassword(kPassword));
  HAuth::init();
  CHECK(HAuth::verify(kPassword));
}

void checkKeys() {
  HCoreLibTest::clearConfigDir();
  HConfig::init();
  HAuth::init();
  REQUIRE(HAuth::setPassword(kPassword));

  char key[HAUTH_KEY_BUFFER_SIZE];
  REQUIRE(HAuth::issueKey(key, sizeof(key)));
  CHECK(std::strlen(key) == HAUTH_KEY_BUFFER_SIZE - 1);

  CHECK(HAuth::checkKey(key));
  CHECK(!HAuth::checkKey("not a key"));
  CHECK(!HAuth::checkKey(""));
  CHECK(!HAuth::checkKey(nullptr));

  // One key at a time: issuing a new one invalidates the old, so a stolen key
  // stops working the moment its owner logs in again.
  char second[HAUTH_KEY_BUFFER_SIZE];
  REQUIRE(HAuth::issueKey(second, sizeof(second)));
  CHECK(std::strcmp(key, second) != 0);
  CHECK(!HAuth::checkKey(key));
  CHECK(HAuth::checkKey(second));

  HAuth::clearKey();
  CHECK(!HAuth::checkKey(second));

  // A buffer that cannot hold a key is refused rather than truncated into a
  // key that would never match.
  char tooSmall[8];
  CHECK(!HAuth::issueKey(tooSmall, sizeof(tooSmall)));
}

}  // namespace

void runAuthTests() noexcept {
  HCoreLibTest::begin("HAuth");

  checkFreshDeviceIsOpen();
  checkSetAndVerify();
  checkStoredAsHash();
  checkPersistence();
  checkTruncatedStoredHash();
  checkKeys();

  HCoreLibTest::clearConfigDir();
}
