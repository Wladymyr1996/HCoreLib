#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief One contiguous block of Unicode codepoints and the glyphs for it.
 *
 * This is what makes a font with Latin AND Cyrillic affordable. The two blocks
 * sit 900 codepoints apart (0x20 and 0x0410); one flat table indexed by
 * codepoint would spend ~5 KB on the hole between them. A font instead
 * declares the blocks it actually has, and lookup walks the list.
 *
 * A POD with no behaviour, so it lives in this header rather than earning a
 * file pair: there is nothing for a .cpp of its own to hold and no vtable to
 * anchor.
 */
struct HFontRange {
  /** First codepoint this block covers, e.g. 0x20 for Latin, 0x0410 for Cyrillic. */
  uint32_t firstCode;

  /** How many consecutive codepoints follow it. */
  uint16_t count;

  /** `count * (width * pages)` bytes, one glyph after another. */
  const uint8_t* glyphs;
};

/**
 * @brief What a font must answer, independent of its size.
 *
 * Exists so HSSD1306::drawText() can take any font at all - a 6x8 and an 8x16
 * are different C++ types (HFont is a template on its dimensions), and without
 * this they could not be passed through the same parameter or held in the same
 * pointer.
 *
 * Do not derive from this directly: derive from HFont<WIDTH, HEIGHT>, which
 * implements every method here and adds the codepoint mapping. A concrete font
 * then supplies nothing but its glyph tables and the ranges they cover.
 */
class HIFont {
 public:
  virtual ~HIFont();

  HIFont(const HIFont&) = delete;
  HIFont& operator=(const HIFont&) = delete;

  /** @brief Glyph width in pixels. Fixed for every glyph in the font. */
  virtual uint8_t width() const = 0;

  /** @brief Glyph height in pixels. */
  virtual uint8_t height() const = 0;

  /** @brief Glyph height in 8-pixel pages - the unit the panel addresses in. */
  virtual uint8_t pages() const = 0;

  /**
   * @brief The glyph for a Unicode codepoint.
   *
   * NEVER returns null. A codepoint no range covers yields the font's
   * "missing character" glyph - a hollow rectangle - because a display that
   * shows a visible box for an unmapped character tells you what is wrong,
   * while one that silently skips it does not.
   *
   * @return `width() * pages()` bytes, ordered page by page, LSB at the top.
   */
  virtual const uint8_t* glyph(uint32_t codepoint) const = 0;

  /** @brief Sugar for glyph(). `font['A']` and `font[0x0410]` both work. */
  const uint8_t* operator[](uint32_t codepoint) const {
    return glyph(codepoint);
  }

 protected:
  HIFont() = default;
};
