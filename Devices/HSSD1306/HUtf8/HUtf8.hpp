#pragma once

#include <cstdint>

/**
 * @brief Decodes UTF-8 text into Unicode codepoints. Static methods only.
 *
 * The display speaks codepoints - a font maps 0x0410 to 'А' - while C string
 * literals in a UTF-8 source file are bytes. "Привіт" is twelve bytes and six
 * characters, and something has to turn one into the other. This is that
 * something, and it is deliberately the whole of it: no normalisation, no
 * combining marks, no case mapping.
 */
class HUtf8 {
 public:
  HUtf8() = delete;

  /** @brief U+FFFD, returned for any byte sequence that is not valid UTF-8. */
  static const uint32_t kReplacement = 0xFFFDu;

  /**
   * @brief Reads one codepoint and advances `cursor` past it.
   *
   * A malformed or truncated sequence yields kReplacement and advances exactly
   * one byte - never zero, because a decoder that fails to advance turns a
   * corrupt string into an infinite loop.
   *
   * @param cursor Reference to the read position. Advanced past the codepoint.
   * @return The codepoint, or 0 at the end of the string (cursor left on the
   *         terminator, so calling again is harmless).
   */
  static uint32_t next(const char*& cursor);
};
