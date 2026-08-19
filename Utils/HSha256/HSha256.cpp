#include "HSha256.hpp"

#include <cstring>

namespace {

/** @brief The first 32 bits of the fractional parts of the cube roots of the first 64 primes. */
const uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

uint32_t rotateRight(uint32_t value, uint32_t bits) noexcept {
  return (value >> bits) | (value << (32u - bits));
}

/** @brief Mixes one 64-byte block into the running state. */
void processBlock(const uint8_t* block, uint32_t* state) noexcept {
  uint32_t w[64];

  // Big-endian, which is the standard's byte order and not this chip's.
  for (uint32_t i = 0; i < 16u; ++i) {
    w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
           (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
           static_cast<uint32_t>(block[i * 4 + 3]);
  }

  for (uint32_t i = 16u; i < 64u; ++i) {
    const uint32_t s0 = rotateRight(w[i - 15], 7) ^ rotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotateRight(w[i - 2], 17) ^ rotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];
  uint32_t e = state[4];
  uint32_t f = state[5];
  uint32_t g = state[6];
  uint32_t h = state[7];

  for (uint32_t i = 0; i < 64u; ++i) {
    const uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
    const uint32_t choice = (e & f) ^ (~e & g);
    const uint32_t temp1 = h + s1 + choice + kRoundConstants[i] + w[i];
    const uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = s0 + majority;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

}  // namespace

void HSha256::hash(const void* data, size_t length, uint8_t* digest) noexcept {
  if (digest == nullptr || (data == nullptr && length != 0)) {
    return;
  }

  // The first 32 bits of the fractional parts of the square roots of the first
  // eight primes.
  uint32_t state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                       0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  size_t remaining = length;

  while (remaining >= 64u) {
    processBlock(bytes, state);
    bytes += 64;
    remaining -= 64;
  }

  // The tail, the 0x80 marker and the 64-bit length. Two blocks are needed when
  // the remainder leaves no room for the length; one otherwise.
  uint8_t tail[128] = {};
  std::memcpy(tail, bytes, remaining);
  tail[remaining] = 0x80u;

  const size_t tailBlocks = (remaining >= 56u) ? 2u : 1u;
  const uint64_t bitLength = static_cast<uint64_t>(length) * 8u;
  const size_t lengthOffset = (tailBlocks * 64u) - 8u;

  for (uint32_t i = 0; i < 8u; ++i) {
    tail[lengthOffset + i] = static_cast<uint8_t>(bitLength >> (56u - (i * 8u)));
  }

  for (size_t block = 0; block < tailBlocks; ++block) {
    processBlock(&tail[block * 64u], state);
  }

  for (uint32_t i = 0; i < 8u; ++i) {
    digest[i * 4] = static_cast<uint8_t>(state[i] >> 24);
    digest[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
    digest[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
    digest[i * 4 + 3] = static_cast<uint8_t>(state[i]);
  }
}

bool HSha256::hashToHex(const char* text, char* outHex, size_t outSize) noexcept {
  if (outHex == nullptr || outSize < HSHA256_HEX_BYTES) {
    return false;
  }

  uint8_t digest[HSHA256_DIGEST_BYTES];
  hash(text, (text != nullptr) ? std::strlen(text) : 0u, digest);

  static const char kHexDigits[] = "0123456789abcdef";
  for (size_t i = 0; i < HSHA256_DIGEST_BYTES; ++i) {
    outHex[i * 2] = kHexDigits[digest[i] >> 4];
    outHex[i * 2 + 1] = kHexDigits[digest[i] & 0x0Fu];
  }
  outHex[HSHA256_DIGEST_BYTES * 2] = '\0';

  return true;
}
