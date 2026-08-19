#include "HUtf8.hpp"

namespace {

/** @brief True for a 10xxxxxx continuation byte. */
bool isContinuation(uint8_t byte) {
  return (byte & 0xC0u) == 0x80u;
}

}  // namespace

uint32_t HUtf8::next(const char*& cursor) {
  if (cursor == nullptr || *cursor == '\0') {
    return 0;
  }

  const uint8_t lead = static_cast<uint8_t>(*cursor);

  // ASCII, and the reason this costs nothing for Latin text.
  if (lead < 0x80u) {
    ++cursor;
    return lead;
  }

  uint32_t codepoint = 0;
  uint8_t remaining = 0;

  if ((lead & 0xE0u) == 0xC0u) {
    codepoint = lead & 0x1Fu;
    remaining = 1;  // 2-byte: Latin-1 supplement, Cyrillic, Greek
  } else if ((lead & 0xF0u) == 0xE0u) {
    codepoint = lead & 0x0Fu;
    remaining = 2;  // 3-byte: the rest of the BMP
  } else if ((lead & 0xF8u) == 0xF0u) {
    codepoint = lead & 0x07u;
    remaining = 3;  // 4-byte: emoji and the supplementary planes
  } else {
    // A continuation byte or 0xFE/0xFF where a lead byte belongs.
    ++cursor;
    return kReplacement;
  }

  const char* lookahead = cursor + 1;
  for (uint8_t index = 0; index < remaining; ++index) {
    if (!isContinuation(static_cast<uint8_t>(*lookahead))) {
      // Truncated or corrupt. Consume ONLY the lead byte, so the bytes that
      // follow still get their chance to be decoded as a fresh sequence.
      ++cursor;
      return kReplacement;
    }
    codepoint = (codepoint << 6) | (static_cast<uint8_t>(*lookahead) & 0x3Fu);
    ++lookahead;
  }

  cursor = lookahead;
  return codepoint;
}
