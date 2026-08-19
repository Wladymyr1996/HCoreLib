#pragma once

#include <cstddef>
#include <cstdint>

#include "../HIFont/HIFont.hpp"

/**
 * @brief Base for every font: fixed glyph size, sparse codepoint mapping.
 *
 * A concrete font derives from this with its dimensions and hands up the list
 * of codepoint blocks it covers. It writes no lookup code at all - the mapping,
 * the bounds checks and the missing-character glyph all live here.
 *
 * @code
 *   class HFont6x8 : public HFont<6, 8> {
 *    public:
 *     HFont6x8();
 *   };
 *
 *   // in the .cpp
 *   const HFontRange kRanges[] = {
 *     {0x20,   91, kLatinGlyphs},      // ' ' .. 'z'
 *     {0x0410, 64, kCyrillicGlyphs},   // 'А' .. 'я'
 *   };
 *   HFont6x8::HFont6x8() : HFont<6, 8>(kRanges, 2) {}
 * @endcode
 *
 * Ranges are searched linearly. A font has a handful of them, so a loop beats
 * anything cleverer in both code size and cache behaviour.
 *
 * ## Glyph layout
 * `width * pages` bytes per glyph, ordered page by page: every column of page
 * 0, then every column of page 1. Each byte is eight vertical pixels with the
 * LSB at the top, which is exactly how the SSD1306 stores display RAM - so a
 * glyph reaches the panel without being transformed.
 *
 * @tparam WIDTH  Glyph width in pixels, spacing included.
 * @tparam HEIGHT Glyph height in pixels. Need not be a multiple of 8.
 */
template <uint8_t WIDTH, uint8_t HEIGHT>
class HFont : public HIFont {
 public:
  /** @brief Height in 8-pixel pages, rounded up. */
  static const uint8_t kPages = static_cast<uint8_t>((HEIGHT + 7) / 8);

  /** @brief Bytes per glyph. */
  static const size_t kGlyphBytes = static_cast<size_t>(WIDTH) * kPages;

  uint8_t width() const override {
    return WIDTH;
  }

  uint8_t height() const override {
    return HEIGHT;
  }

  uint8_t pages() const override {
    return kPages;
  }

  /**
   * @brief Maps a codepoint to its glyph, or to the missing-character box.
   *
   * @see HIFont::glyph(), which states the contract this satisfies.
   */
  const uint8_t* glyph(uint32_t codepoint) const override {
    for (size_t index = 0; index < rangeCount_; ++index) {
      const HFontRange& range = ranges_[index];

      // Half-open on purpose: firstCode + count is the first codepoint the
      // block does NOT cover, so an empty range can never match.
      if (codepoint >= range.firstCode &&
          codepoint < static_cast<uint32_t>(range.firstCode) + range.count) {
        const size_t offset = static_cast<size_t>(codepoint - range.firstCode);
        return range.glyphs + (offset * kGlyphBytes);
      }
    }
    return missing_;
  }

 protected:
  /**
   * @param ranges     Blocks this font covers. Must have static storage
   *                   duration - the pointer is kept, never copied from.
   * @param rangeCount How many.
   */
  HFont(const HFontRange* ranges, size_t rangeCount) : ranges_(ranges), rangeCount_(rangeCount) {
    buildMissingGlyph();
  }

 private:
  /**
   * @brief Draws the hollow rectangle returned for unmapped codepoints.
   *
   * Built at construction rather than stored as a constant, because its shape
   * depends on WIDTH and HEIGHT and every instantiation would otherwise need
   * its own hand-written table. Costs kGlyphBytes of RAM per font object - six
   * bytes for a 6x8 - and runs once, during static initialisation.
   */
  void buildMissingGlyph() {
    for (uint8_t page = 0; page < kPages; ++page) {
      for (uint8_t column = 0; column < WIDTH; ++column) {
        uint8_t bits = 0;

        for (uint8_t bit = 0; bit < 8; ++bit) {
          const uint16_t y = static_cast<uint16_t>((page * 8) + bit);
          if (y >= HEIGHT) {
            break;  // Ragged last page: a 12-pixel font leaves four rows unused.
          }
          const bool onEdge = (y == 0) || (y == HEIGHT - 1) || (column == 0) ||
                              (column == WIDTH - 1);
          if (onEdge) {
            bits = static_cast<uint8_t>(bits | (1u << bit));
          }
        }

        missing_[(static_cast<size_t>(page) * WIDTH) + column] = bits;
      }
    }
  }

  const HFontRange* ranges_;
  size_t rangeCount_;

  uint8_t missing_[kGlyphBytes];
};
