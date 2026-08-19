#pragma once

#include <cstddef>
#include <cstdint>

#include <HCoreLib.h>

/** @brief Characters in an issued key, terminator included. */
#define HAUTH_KEY_BUFFER_SIZE 33

/** How long a key stays usable after its LAST use, in ms. */
#ifndef HAUTH_KEY_TTL_MS
#define HAUTH_KEY_TTL_MS 900000
#endif

/** HConfig module holding the password hash, giving `config/auth.cfg`. */
#ifndef HAUTH_CONFIG_MODULE
#define HAUTH_CONFIG_MODULE "auth"
#endif

/**
 * @brief The admin password, and the session key the REST API is guarded by.
 *
 * ## The password is stored as a hash
 * `config/auth.cfg` holds a SHA-256 of the password, never the password. The
 * device only ever needs to answer "is this the same string", which a hash
 * answers, and the file is reachable by anything that can read the filesystem -
 * so what is in it should not be the secret itself. See HSha256 on what this
 * does and does not buy.
 *
 * ## A fresh device has no password
 * Until setPassword() succeeds, isPasswordSet() is false and TWO things are
 * open: /api/setAdminPassword needs no key, and /api/auth issues a key for any
 * password at all. That is deliberate - a device that shipped locked would be a
 * device nobody could configure - and every call while it lasts is logged as a
 * warning, because it is a state to leave rather than to live in.
 *
 * ## One key at a time
 * A key is 32 hex characters from the hardware random generator, kept in RAM
 * only, and it expires HAUTH_KEY_TTL_MS after its last use. Issuing a new one
 * invalidates the old: this is a portal one person stands in front of, not a
 * multi-session service, and a single slot means a stolen key stops working the
 * moment its owner logs in again.
 */
class HAuth {
 public:
  HAuth() = delete;

  /**
   * @brief Loads the stored password hash. Call once, after HConfig::init().
   */
  static void init() noexcept;

  /** @brief False on a device that has never had a password set. */
  static bool isPasswordSet() noexcept;

  /**
   * @brief Replaces the admin password and persists its hash.
   * @param password Plain text, as it arrived from the API. Not stored.
   * @return false if the password is empty or the file could not be written.
   */
  static bool setPassword(const char* password) noexcept;

  /**
   * @brief True if `password` matches the stored one.
   *
   * Always TRUE while no password is set - see the class docs on why a fresh
   * device is deliberately open.
   */
  static bool verify(const char* password) noexcept;

  /**
   * @brief Issues a fresh key, invalidating any previous one.
   * @param outKey Receives HAUTH_KEY_BUFFER_SIZE characters.
   * @param outSize Size of outKey.
   * @return false if the buffer is too small.
   */
  static bool issueKey(char* outKey, size_t outSize) noexcept;

  /**
   * @brief Checks a key from the `Authentication-Info` header and refreshes its life.
   * @param key Header value; nullptr and empty are rejected.
   * @return false if there is no key, it does not match, or it has expired.
   */
  static bool checkKey(const char* key) noexcept;

  /** @brief Drops the current key, so every guarded route refuses until the next login. */
  static void clearKey() noexcept;
};
