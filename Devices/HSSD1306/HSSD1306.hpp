#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <driver/i2c_master.h>

#include "HIFont/HIFont.hpp"
#include "HSSD1306Panel/HSSD1306Panel.hpp"
#include "HUtf8/HUtf8.hpp"

/**
 * @brief An SSD1306 / SSD1315 / SSH1106 OLED panel of a given size.
 *
 * Ported from the SSD1306xLED AVR driver in `3rdParty/SSD1315` (Neven
 * Boyanov, Tinusaur), keeping its init sequence and its fonts. Two things
 * changed, both because an ESP32 is not an ATtiny85:
 *
 * 1. **There is a framebuffer.** The original drew straight down the wire
 *    because it had 512 bytes of RAM. That is why it needed `compose_bmp_px`,
 *    `clear_area_px` and `send_buf`: the panel cannot be read back over I2C,
 *    so two sprites sharing a page erased each other unless the caller
 *    composited them by hand. Here the page is in RAM - drawing is
 *    read-modify-write, overlap just works, and all of that is gone.
 *
 * 2. **I2C is the peripheral's job.** The bus is borrowed, never owned, so a
 *    temperature sensor can share it.
 *
 * The size is a template parameter rather than a macro so that two panels of
 * different sizes can coexist in one firmware, and so the framebuffer is sized
 * exactly by the type. Everything that does not depend on the size lives in
 * HSSD1306Panel and is compiled once, however many sizes are instantiated.
 *
 * Nothing reaches the panel until flush(). Every drawing call touches RAM
 * only, so a frame costs one burst of traffic rather than one per operation.
 *
 * @code
 *   i2c_master_bus_handle_t bus = nullptr;
 *   i2c_new_master_bus(&busConfig, &bus);
 *
 *   HSSD1306<128, 64> oled(bus);
 *   oled.begin();
 *
 *   oled.drawText(0, 0, "Двері", HFonts::font8x16);   // UTF-8 in, glyphs out
 *   oled.drawText(0, 24, "opened", HFonts::font6x8);
 *   oled.flush();
 * @endcode
 *
 * ## Not for the scan
 * flush() blocks on I2C. A full 64-pixel frame is ~1 KB at 400 kHz, roughly
 * **25 ms** - more than two engine scans. Only dirty pages are sent, so a
 * one-line update is nearer 3 ms, but this still belongs on its own task or in
 * startup, never inside a unit's update() or a boundary's read()/write().
 *
 * Not thread-safe: one task owns one display.
 *
 * @tparam WIDTH  Panel width in pixels, normally 128.
 * @tparam HEIGHT Panel height in pixels: 64 or 32.
 */
template <uint16_t WIDTH, uint16_t HEIGHT>
class HSSD1306 : public HSSD1306Panel {
 public:
  /** @brief Height in 8-pixel pages - the unit the controller addresses in. */
  static const uint8_t kPages = static_cast<uint8_t>(HEIGHT / 8);

  static_assert(HEIGHT % 8 == 0, "HEIGHT must be a whole number of 8-pixel pages");
  static_assert(HEIGHT / 8 <= 8, "HEIGHT must fit the 8-bit dirty-page mask");
  static_assert(WIDTH > 0, "WIDTH must be positive");

  /**
   * @param bus     An open bus from i2c_new_master_bus(). Borrowed - it must
   *                outlive this object. Stored only; nothing is transmitted
   *                until begin(), because a constructor cannot report failure.
   * @param address 7-bit I2C address.
   * @param speedHz Clock for this device.
   */
  explicit HSSD1306(i2c_master_bus_handle_t bus, uint8_t address = HSSD1306_ADDRESS,
                    uint32_t speedHz = HSSD1306_I2C_SPEED_HZ)
      : bus_(bus), address_(address), speedHz_(speedHz), dirty_(0) {
    clear(false);
  }

  /** @brief Panel width in pixels - the template argument, as a runtime value. */
  uint16_t width() const override {
    return WIDTH;
  }

  /** @brief Panel height in pixels - the template argument, as a runtime value. */
  uint16_t height() const override {
    return HEIGHT;
  }

  /**
   * @brief Configures the panel and clears it.
   *
   * @return false if the panel is absent or refused the init sequence. Drawing
   *         still works afterwards - it is only RAM - and flush() becomes a
   *         no-op, so a missing display degrades the node rather than stopping
   *         it.
   */
  bool begin() {
    if (!configure(bus_, address_, speedHz_, static_cast<uint8_t>(HEIGHT))) {
      return false;
    }

    clear(false);
    return flush();
  }

  // --------------------------------------------------------------------------
  // Drawing. RAM only - nothing reaches the panel until flush().
  // --------------------------------------------------------------------------

  /** @brief Fills the whole framebuffer with off (or on) pixels. */
  void clear(bool on = false) {
    const uint8_t fill = on ? 0xFF : 0x00;

    for (uint8_t page = 0; page < kPages; ++page) {
      frame_[page][0] = kControlData;  // The prefix is part of the buffer, always.
      memset(pixels(page), fill, WIDTH);
    }
    invalidate();
  }

  /** @brief Marks every page dirty, so the next flush() resends the whole panel. */
  void invalidate() {
    dirty_ = (kPages >= 8) ? 0xFFu : static_cast<uint8_t>((1u << kPages) - 1u);
  }

  /** @brief Sets one pixel. Coordinates outside the panel are ignored, not wrapped. */
  void setPixel(int16_t x, int16_t y, bool on) {
    if (x < 0 || y < 0 || x >= static_cast<int16_t>(WIDTH) || y >= static_cast<int16_t>(HEIGHT)) {
      return;  // Clipped, never wrapped - a wrapped pixel hides a caller's bug.
    }

    const uint8_t page = static_cast<uint8_t>(y) >> 3;
    const uint8_t bit = static_cast<uint8_t>(1u << (static_cast<uint8_t>(y) & 0x07));

    if (on) {
      pixels(page)[x] |= bit;
    } else {
      pixels(page)[x] &= static_cast<uint8_t>(~bit);
    }
    dirty_ |= static_cast<uint8_t>(1u << page);
  }

  /** @brief Fills a rectangle. Clipped to the panel; a zero or negative size draws nothing. */
  void fillRect(int16_t x, int16_t y, int16_t width, int16_t height, bool on) {
    for (int16_t row = y; row < y + height; ++row) {
      for (int16_t column = x; column < x + width; ++column) {
        setPixel(column, row, on);
      }
    }
  }

  /**
   * @brief Draws a bitmap in the panel's own column-major page format.
   *
   * `y` is in PIXELS, not pages: a bitmap that does not land on a page
   * boundary is shifted across two pages as it is written, which is what the
   * original driver's `draw_bmp_px` did on the wire and this does in RAM.
   *
   * @param bitmap `width * pages` bytes, ordered page by page.
   * @param merge  true ORs the bitmap into what is already there (a sprite
   *               over a background); false replaces those pixels. Replacing
   *               touches only the bitmap's own pixel rows, not the whole page
   *               - something the AVR version could not offer at all.
   */
  void drawBitmap(int16_t x, int16_t y, uint8_t width, uint8_t pages, const uint8_t* bitmap,
                  bool merge = false) {
    if (bitmap == nullptr || width == 0 || pages == 0) {
      return;
    }

    const int16_t offset = static_cast<int16_t>(y & 0x07);
    const int16_t firstPage = static_cast<int16_t>(y >> 3);
    const int16_t touchedPages = static_cast<int16_t>(pages + ((offset != 0) ? 1 : 0));

    for (int16_t relative = 0; relative < touchedPages; ++relative) {
      const int16_t page = firstPage + relative;
      if (page < 0 || page >= static_cast<int16_t>(kPages)) {
        continue;
      }

      uint8_t* destination = pixels(static_cast<uint8_t>(page));

      for (uint8_t column = 0; column < width; ++column) {
        const int16_t screenX = static_cast<int16_t>(x + column);
        if (screenX < 0 || screenX >= static_cast<int16_t>(WIDTH)) {
          continue;
        }

        uint8_t bits = 0;
        uint8_t mask = 0xFF;  // Which bits of this page the bitmap actually covers.

        if (offset == 0) {
          bits = bitmap[(relative * width) + column];
        } else {
          if (relative > 0) {
            bits = static_cast<uint8_t>(bitmap[((relative - 1) * width) + column] >> (8 - offset));
          }
          if (relative < pages) {
            bits |= static_cast<uint8_t>(bitmap[(relative * width) + column] << offset);
          }

          // Only the rows the bitmap really occupies may be replaced. Without
          // this, a sprite at y=4 would blank the four pixel rows above it.
          if (relative == 0) {
            mask = static_cast<uint8_t>(0xFF << offset);
          } else if (relative == pages) {
            mask = static_cast<uint8_t>(0xFF >> (8 - offset));
          }
        }

        if (merge) {
          destination[screenX] |= bits;
        } else {
          destination[screenX] =
              static_cast<uint8_t>((destination[screenX] & ~mask) | (bits & mask));
        }
      }

      dirty_ |= static_cast<uint8_t>(1u << page);
    }
  }

  /**
   * @brief Draws UTF-8 text with the given font. `y` is in pixels.
   *
   * The text is decoded to codepoints and each one is looked up in the font,
   * so "Двері" draws Cyrillic if the font declares that range and a row of
   * hollow boxes if it does not - never mojibake, and never silence.
   *
   * There is no line wrapping: anything past the right edge is clipped,
   * because a display driver guessing where a line should break is a decision
   * that belongs to the caller.
   *
   * @return The x coordinate just past the last glyph, so calls can be chained.
   */
  int16_t drawText(int16_t x, int16_t y, const char* text, const HIFont& font,
                   bool merge = false) {
    if (text == nullptr) {
      return x;
    }

    const uint8_t glyphWidth = font.width();
    const uint8_t glyphPages = font.pages();
    int16_t cursor = x;

    const char* reader = text;
    for (uint32_t codepoint = HUtf8::next(reader); codepoint != 0;
         codepoint = HUtf8::next(reader)) {
      drawBitmap(cursor, y, glyphWidth, glyphPages, font.glyph(codepoint), merge);
      cursor = static_cast<int16_t>(cursor + glyphWidth);
    }

    return cursor;
  }

  /** @brief Width in pixels drawText() would occupy. Counts CHARACTERS, not bytes. */
  static uint16_t textWidth(const char* text, const HIFont& font) {
    if (text == nullptr) {
      return 0;
    }

    uint16_t characters = 0;
    const char* reader = text;
    while (HUtf8::next(reader) != 0) {
      ++characters;
    }
    return static_cast<uint16_t>(characters * font.width());
  }

  // --------------------------------------------------------------------------
  // The panel itself.
  // --------------------------------------------------------------------------

  /**
   * @brief Sends every page whose contents changed since the last flush.
   *
   * Clean pages cost nothing, so redrawing one line of text is one page of
   * traffic rather than eight. A page that fails to transmit stays dirty and
   * is retried by the next flush, rather than leaving the panel showing half a
   * frame forever.
   *
   * @return false if any transfer failed.
   */
  /** @brief True while any page still needs sending. */
  bool isDirty() const {
    return dirty_ != 0;
  }

  /**
   * @brief Sends AT MOST ONE dirty page, lowest first. For callers that must not block.
   *
   * flush() is 25 ms for a full 128x64 frame, which is two and a half engine
   * scans - it can never be called from a boundary. This is the same work split
   * the way HBoundary demands: one page is ~129 bytes, about 3 ms at 400 kHz,
   * and a worst-case whole-frame repaint spreads over eight scans.
   *
   * @return true if a page was sent - call again while isDirty() to finish the
   *         frame. false when nothing was dirty, or the transfer failed and the
   *         page stayed dirty for the next attempt.
   */
  bool flushPage() {
    if (!isReady()) {
      return false;
    }

    for (uint8_t page = 0; page < kPages; ++page) {
      const uint8_t bit = static_cast<uint8_t>(1u << page);
      if ((dirty_ & bit) == 0) {
        continue;
      }

      if (!sendPage(page, frame_[page], 1 + WIDTH)) {
        return false;  // Left dirty on purpose: the next call retries it.
      }

      dirty_ &= static_cast<uint8_t>(~bit);
      return true;
    }

    return false;
  }

  bool flush() {
    if (!isReady()) {
      return false;
    }

    bool ok = true;

    for (uint8_t page = 0; page < kPages; ++page) {
      const uint8_t bit = static_cast<uint8_t>(1u << page);
      if ((dirty_ & bit) == 0) {
        continue;
      }

      if (!sendPage(page, frame_[page], 1 + WIDTH)) {
        ok = false;
        continue;
      }
      dirty_ &= static_cast<uint8_t>(~bit);
    }

    return ok;
  }

 private:
  /** I2C control byte: everything after it is display data. */
  static const uint8_t kControlData = 0x40;

  /** @brief The pixel bytes of a page, skipping its control byte. */
  uint8_t* pixels(uint8_t page) {
    return &frame_[page][1];
  }

  i2c_master_bus_handle_t bus_;
  uint8_t address_;
  uint32_t speedHz_;

  /**
   * The framebuffer, one row per page, each row prefixed by its own 0x40
   * "the rest of this is display data" control byte. Carrying the prefix
   * inside the buffer is what lets a page be handed to i2c_master_transmit()
   * directly - no assembly buffer, no copy, no scatter-gather.
   */
  uint8_t frame_[kPages][1 + WIDTH];

  /** One bit per page, set by drawing and cleared by flush(). */
  uint8_t dirty_;
};
