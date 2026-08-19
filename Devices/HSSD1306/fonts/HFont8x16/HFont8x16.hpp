#pragma once

#include "../../HFont/HFont.hpp"

/**
 * @brief 8x16 font: Latin from the SSD1306xLED reference, Cyrillic to be drawn.
 *
 * Two pages tall, eight columns per glyph. Covers:
 *
 * | Block | Codepoints | State |
 * | --- | --- | --- |
 * | Latin | ' ' (0x20) .. '~' (0x7E) | drawn |
 * | Cyrillic | U+0410 'А' .. U+044F 'я' | EMPTY, to be drawn |
 * | Ukrainian | U+0404 Є, U+0406 І, U+0407 Ї, U+0490 Ґ and lowercase | EMPTY, to be drawn |
 *
 * An empty glyph draws nothing at all; only a codepoint no block covers gets
 * the missing-character box. So a half-finished font shows gaps where letters
 * belong, and boxes where the font simply has no answer.
 *
 * All the lookup logic is HFont's. This class exists only to name the tables.
 */
class HFont8x16 : public HFont<8, 16> {
 public:
  HFont8x16();
};

/** @brief Ready-made font instances, so callers need not build their own. */
namespace HFonts {

/** @brief The shared 8x16 font. */
extern const HFont8x16 font8x16;

}  // namespace HFonts
