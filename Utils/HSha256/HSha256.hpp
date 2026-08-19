#pragma once

#include <cstddef>
#include <cstdint>

/** @brief Bytes in a SHA-256 digest. */
#define HSHA256_DIGEST_BYTES 32

/** @brief Buffer size for a hex digest, terminator included. */
#define HSHA256_HEX_BYTES 65

/**
 * @brief SHA-256, in about a hundred lines and no dependencies.
 *
 * Deliberately not mbedtls, which is only there on the target: this way the code
 * that hashes an admin password is the SAME code on the device and in a host
 * test, and can be checked against the published vectors rather than trusted.
 *
 * It is a plain hash, not a password KDF. For a device whose configuration
 * portal is only reachable in Configuring mode, with an attacker who would need
 * the filesystem contents first, it buys the thing that matters: the stored
 * value is not the password, so reading the file does not hand anyone the
 * secret. If this ever guards something reachable from the internet, replace it
 * with a salted, iterated derivation - the seam is HAuth, which is the only
 * caller.
 */
class HSha256 {
 public:
  HSha256() = delete;

  /**
   * @brief Hashes a buffer.
   * @param data Bytes to hash; may be null only when length is 0.
   * @param length Byte count.
   * @param digest Receives HSHA256_DIGEST_BYTES bytes.
   */
  static void hash(const void* data, size_t length, uint8_t* digest) noexcept;

  /**
   * @brief Hashes a null-terminated string into lowercase hex.
   * @param text String to hash; nullptr hashes as an empty string.
   * @param outHex Receives HSHA256_HEX_BYTES characters, terminator included.
   * @param outSize Size of outHex; must be at least HSHA256_HEX_BYTES.
   * @return false if the buffer is too small, in which case nothing is written.
   */
  static bool hashToHex(const char* text, char* outHex, size_t outSize) noexcept;
};
