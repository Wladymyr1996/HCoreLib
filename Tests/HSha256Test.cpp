#include "HCoreLibTest.hpp"

#include <cstring>

#include <HSha256/HSha256.hpp>

/**
 * @file HSha256Test.cpp
 * @brief The published vectors, which is the whole reason this hash is ours.
 *
 * HSha256 exists instead of mbedtls so that the code hashing an admin password
 * is the same code on the device and here - and the only thing that makes that
 * worth anything is checking it against values somebody else published. These
 * are FIPS 180-4's.
 */

namespace {

/** @brief Renders a raw digest as lowercase hex, the way hashToHex() reports it. */
void toHex(const uint8_t* digest, char* out) {
  static const char kDigits[] = "0123456789abcdef";
  for (size_t i = 0; i < HSHA256_DIGEST_BYTES; ++i) {
    out[i * 2] = kDigits[digest[i] >> 4];
    out[i * 2 + 1] = kDigits[digest[i] & 0x0F];
  }
  out[HSHA256_DIGEST_BYTES * 2] = '\0';
}

void checkKnownVectors() {
  char hex[HSHA256_HEX_BYTES];

  REQUIRE(HSha256::hashToHex("", hex, sizeof(hex)));
  CHECK_TEXT(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  REQUIRE(HSha256::hashToHex("abc", hex, sizeof(hex)));
  CHECK_TEXT(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  // 56 bytes: the case that needs a second block for the length padding, and
  // the one a hand-written implementation gets wrong.
  REQUIRE(HSha256::hashToHex(
      "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", hex, sizeof(hex)));
  CHECK_TEXT(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

  // 112 bytes, so the message and its length padding straddle three blocks.
  REQUIRE(HSha256::hashToHex(
      "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnop"
      "jklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
      hex, sizeof(hex)));
  CHECK_TEXT(hex, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

void checkRawDigest() {
  uint8_t digest[HSHA256_DIGEST_BYTES];
  HSha256::hash("abc", 3, digest);

  char hex[HSHA256_HEX_BYTES];
  toHex(digest, hex);
  CHECK_TEXT(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  // A null buffer is allowed only at zero length, and hashes as the empty
  // string rather than faulting.
  HSha256::hash(nullptr, 0, digest);
  toHex(digest, hex);
  CHECK_TEXT(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

void checkBufferHandling() {
  char hex[HSHA256_HEX_BYTES];

  // nullptr hashes as the empty string - HAuth passes whatever arrived on the
  // socket, and a missing field is not a crash.
  REQUIRE(HSha256::hashToHex(nullptr, hex, sizeof(hex)));
  CHECK_TEXT(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  // One byte short is refused, and refused BEFORE anything is written.
  char tooSmall[HSHA256_HEX_BYTES - 1];
  std::memset(tooSmall, 'Z', sizeof(tooSmall));
  CHECK(!HSha256::hashToHex("abc", tooSmall, sizeof(tooSmall)));
  CHECK(tooSmall[0] == 'Z');
}

}  // namespace

void runSha256Tests() noexcept {
  HCoreLibTest::begin("HSha256");

  checkKnownVectors();
  checkRawDigest();
  checkBufferHandling();
}
