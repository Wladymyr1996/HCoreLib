#include "HAuth/HAuth.hpp"

#define HLOG_MODULE_NAME "Auth"
#include <HLog/HLog.hpp>

#include <cstring>

#include <etl/mutex.h>
#include <etl/span.h>

#include <HConfig/HConfig.hpp>
#include <HSha256/HSha256.hpp>
#include <HSystemUtils/HSystemUtils.hpp>



#if IS_MCU
#include <esp_random.h>
#else
#include <cstdlib>
#endif

namespace {

/** @brief The only key in config/auth.cfg. */
const char* const kKeyPasswordHash = "hash";

char storedHash[HSHA256_HEX_BYTES] = "";
char activeKey[HAUTH_KEY_BUFFER_SIZE] = "";
uint32_t keyTouchedMs = 0;

/**
 * @brief Guards the hash and the key.
 *
 * The HTTP server answers from its own task, so every function here can be
 * entered while another is running.
 */
etl::mutex& authMutex() noexcept {
  static etl::mutex mutex;
  return mutex;
}

/** @brief Four random bytes, from the best source the platform has. */
uint32_t randomWord() noexcept {
#if IS_MCU
  // Hardware RNG. Genuinely random once the radio is up, which in this mode it
  // always is - Configuring mode starts Wi-Fi before the web server.
  return esp_random();
#else
  // The host build exists to test the logic around keys, not to issue real
  // ones. Nothing on a desktop is guarding anything.
  return static_cast<uint32_t>(std::rand()) ^ (static_cast<uint32_t>(std::rand()) << 16);
#endif
}

/**
 * @brief Compares two strings in constant time.
 *
 * strcmp returns at the first differing byte, so how long it takes says how much
 * of a guess was right. That is a timing oracle a patient attacker can walk a
 * key out of, one character at a time. This looks at every byte either way.
 */
bool constantTimeEquals(const char* a, const char* b, size_t length) noexcept {
  uint8_t difference = 0;
  for (size_t i = 0; i < length; ++i) {
    difference |= static_cast<uint8_t>(a[i] ^ b[i]);
  }
  return difference == 0;
}

}  // namespace

void HAuth::init() noexcept {
  const HValue stored = HConfig::read(HAUTH_CONFIG_MODULE, kKeyPasswordHash, HValue(""));

  etl::lock_guard<etl::mutex> lock(authMutex());
  std::snprintf(storedHash, sizeof(storedHash), "%s", stored.asString().c_str());

  if (storedHash[0] == '\0') {
    HWarning("no admin password set - the API is OPEN until one is");
  } else {
    HInfo("admin password is set");
  }
}

bool HAuth::isPasswordSet() noexcept {
  etl::lock_guard<etl::mutex> lock(authMutex());
  return storedHash[0] != '\0';
}

bool HAuth::setPassword(const char* password) noexcept {
  if (password == nullptr || password[0] == '\0') {
    HWarning("refusing to set an empty admin password");
    return false;
  }

  char hash[HSHA256_HEX_BYTES] = "";
  if (!HSha256::hashToHex(password, hash, sizeof(hash))) {
    return false;
  }

  const HConfigEntry entries[] = {HConfigEntry(kKeyPasswordHash, HValue(hash))};
  if (!HConfig::write(HAUTH_CONFIG_MODULE, etl::span<const HConfigEntry>(entries, 1))) {
    HCritical("could not write config/%s.cfg - the password is unchanged", HAUTH_CONFIG_MODULE);
    return false;
  }

  {
    etl::lock_guard<etl::mutex> lock(authMutex());
    std::memcpy(storedHash, hash, sizeof(storedHash));

    // Any key issued under the old password dies with it. Changing the password
    // has to end the sessions that the old one opened.
    activeKey[0] = '\0';
  }

  HInfo("admin password changed - existing keys are now invalid");
  return true;
}

bool HAuth::verify(const char* password) noexcept {
  char hash[HSHA256_HEX_BYTES] = "";
  const bool hashed = HSha256::hashToHex(password, hash, sizeof(hash));

  etl::lock_guard<etl::mutex> lock(authMutex());

  if (storedHash[0] == '\0') {
    // Deliberately open - see the class docs. Loud, because it is a state to
    // leave rather than to live in.
    HWarning("accepting any password: none is set on this device");
    return true;
  }

  if (!hashed || password == nullptr) {
    return false;
  }

  return constantTimeEquals(hash, storedHash, HSHA256_HEX_BYTES - 1);
}

bool HAuth::issueKey(char* outKey, size_t outSize) noexcept {
  if (outKey == nullptr || outSize < HAUTH_KEY_BUFFER_SIZE) {
    return false;
  }

  static const char kHexDigits[] = "0123456789abcdef";
  char key[HAUTH_KEY_BUFFER_SIZE] = "";

  // 128 bits, in eight random words rather than 32 separate calls.
  for (size_t word = 0; word < 4; ++word) {
    const uint32_t value = randomWord();
    for (size_t nibble = 0; nibble < 8; ++nibble) {
      key[(word * 8) + nibble] = kHexDigits[(value >> (28 - (nibble * 4))) & 0x0Fu];
    }
  }
  key[HAUTH_KEY_BUFFER_SIZE - 1] = '\0';

  {
    etl::lock_guard<etl::mutex> lock(authMutex());
    std::memcpy(activeKey, key, sizeof(activeKey));
    keyTouchedMs = HSystemUtils::millis();
  }

  std::memcpy(outKey, key, HAUTH_KEY_BUFFER_SIZE);
  HInfo("issued a session key");
  return true;
}

bool HAuth::checkKey(const char* key) noexcept {
  if (key == nullptr || key[0] == '\0') {
    return false;
  }

  etl::lock_guard<etl::mutex> lock(authMutex());

  if (activeKey[0] == '\0') {
    return false;
  }

  // Unsigned subtraction, so this stays right across the 49.7-day rollover of
  // millis() - the same rule HTimer follows.
  if ((HSystemUtils::millis() - keyTouchedMs) > HAUTH_KEY_TTL_MS) {
    activeKey[0] = '\0';
    HInfo("session key expired");
    return false;
  }

  if (std::strlen(key) != HAUTH_KEY_BUFFER_SIZE - 1) {
    return false;
  }

  if (!constantTimeEquals(key, activeKey, HAUTH_KEY_BUFFER_SIZE - 1)) {
    return false;
  }

  // Every accepted request extends the session, so somebody working through a
  // settings page is not logged out mid-edit.
  keyTouchedMs = HSystemUtils::millis();
  return true;
}

void HAuth::clearKey() noexcept {
  etl::lock_guard<etl::mutex> lock(authMutex());
  activeKey[0] = '\0';
}
