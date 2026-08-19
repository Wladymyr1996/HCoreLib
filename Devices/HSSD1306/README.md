# SSD1306 — monochrome OLED panel

Driver for SSD1306 / SSD1315 / SSH1106 OLED panels over I2C, with a
framebuffer, a UTF-8 text layer and three fonts covering Latin, Cyrillic and
Ukrainian.

| | |
|---|---|
| Header | `lib/ssd1306/HSSD1306/HSSD1306.hpp` |
| Class | `HSSD1306<WIDTH, HEIGHT>` |
| I2C address | `0x3C`, or `0x3D` with the jumper moved |
| Bus speed | 400 kHz |
| Full frame | ~25 ms at 128×64 |
| RAM | `pages × (1 + width)` bytes — 1032 for 128×64 |

Ported from the SSD1306xLED AVR driver in `3rdParty/SSD1315` (Neven Boyanov,
Tinusaur), keeping its init sequence and fonts.

## Wiring

Four pins: VCC (3.3 V), GND, SCL, SDA. Every module of this kind carries its
own 4.7 kΩ pull-up pair. It shares the bus with the sensors — on this board
`I2C_NUM_0`, SDA on GPIO1 and SCL on GPIO0, opened by `openBus()` in
`App/main.cpp`.

## Quick start

```cpp
#include "lib/ssd1306/HSSD1306/HSSD1306.hpp"
#include "lib/ssd1306/fonts/HFont8x16/HFont8x16.hpp"

// Static, not a local: 1 KB of framebuffer on a 3.5 KB task stack is a
// stack-protection fault waiting to happen.
static HSSD1306<128, 64> oled(bus);

if (!oled.begin()) {
  HWarning("no panel at 0x%02X", HSSD1306_ADDRESS);
}

oled.clear();
oled.drawText(0, 0, "Двері", HFonts::font8x16);   // UTF-8 in, glyphs out
oled.fillRect(0, 17, 128, 1, true);               // a rule under it
oled.flush();                                     // nothing reaches the panel until here
```

The size is a template parameter, so two panels of different sizes can coexist
in one firmware and each framebuffer is sized exactly by its type. `HEIGHT`
must be a whole number of 8-pixel pages, at most 64.

## Nothing reaches the panel until `flush()`

Every drawing call touches RAM only. A frame costs one burst of traffic rather
than one burst per operation, and overlapping draws simply work — the page is
in RAM, so drawing is read-modify-write.

`flush()` sends only the pages that changed since the last call. A full 128×64
frame is ~25 ms; a one-line update is nearer 3 ms. A page that fails to
transmit stays dirty and is retried by the next `flush()`, rather than leaving
the panel showing half a frame forever.

**`flush()` blocks on I2C.** Own task or startup — never a unit's `update()` or
a boundary's `read()`/`write()`.

For a caller that genuinely cannot block — a boundary on a 10 ms scan — use
`flushPage()` instead. It sends one page (~129 bytes, about 3 ms) per call, so
a whole-frame repaint spreads over eight scans rather than blowing an 80 ms
hole in one:

```cpp
if (somethingChanged()) {
  repaint();          // RAM only, microseconds
}
oled.flushPage();     // at most one page of traffic per scan
```

`HOledBoundary` is the worked example.

## API

### Drawing — RAM only

| Method | Notes |
|---|---|
| `clear(bool on = false)` | Fills the whole framebuffer. |
| `invalidate()` | Marks every page dirty; the next `flush()` resends everything. |
| `setPixel(x, y, on)` | Out-of-range coordinates are **clipped, never wrapped** — a wrapped pixel hides a caller's bug. |
| `fillRect(x, y, w, h, on)` | Clipped. Zero or negative size draws nothing. |
| `drawBitmap(x, y, w, pages, bitmap, merge)` | `y` in **pixels**, not pages. |
| `drawText(x, y, text, font, merge)` | UTF-8. Returns the x just past the last glyph, so calls chain. |
| `static textWidth(text, font)` | Counts **characters**, not bytes. |

`merge = true` ORs the new pixels into what is there (a sprite over a
background); `false` replaces them — and touches only the bitmap's own pixel
rows, not the whole page, so a sprite at `y = 4` does not blank the four rows
above it.

There is no line wrapping. Anything past the right edge is clipped: where a
line should break is the caller's decision, not the driver's.

### The panel

| Method | Notes |
|---|---|
| `bool begin()` | Configure and clear. `false` if the panel is absent or refused the init sequence. |
| `bool flush()` | Send every dirty page. `false` if any transfer failed. **~25 ms for a full frame.** |
| `bool flushPage()` | Send at most ONE dirty page, ~3 ms. `true` if one was sent. |
| `bool isDirty() const` | True while any page still needs sending. |
| `bool isReady() const` | True once the panel has acknowledged. |
| `bool setContrast(uint8_t)` | `0x00` dimmest to `0xFF` brightest. Init uses `0x3F`. |
| `bool setDisplayOn(bool)` | Sleeps or wakes. Display RAM survives; current drops to ~1 µA. |
| `bool setInverted(bool)` | Inverts in hardware, without touching the framebuffer. |

A missing panel **degrades the node rather than stopping it**: drawing still
works — it is only RAM — and `flush()` becomes a no-op.

## Fonts

`drawText()` takes any font through the `HIFont` interface. Text is decoded to
Unicode codepoints and each is looked up, so `"Двері"` draws Cyrillic if the
font declares that range and a row of hollow boxes if it does not — never
mojibake, and never silence.

| Font | Size | Codepoints | Flash |
|---|---|---|---|
| `HFonts::font6x8` | 6×8 px | `0x20`–`0x7A`, Cyrillic `А`–`я`, Ukrainian `ЄєІіЇїҐґ` | ~978 B |
| `HFonts::font8x16` | 8×16 px | as above, plus full printable ASCII and `°` (`U+00B0`) | ~2.7 KB |
| `HFonts::font24x32` | 24×32 px | `0x20`–`0x39` only: space, punctuation, digits | ~2.5 KB |

Notes that matter in practice:

- **`°` exists only in `font8x16`.** A temperature line that wants the degree
  sign cannot drop to the small font.
- **`font24x32` has no letters.** It covers space through `'9'` — digits,
  `. - + , / %` and friends — which is exactly enough for a large numeric
  readout like `24.5`, and nothing else.
- At 8 px per character, a 128 px panel fits **16 characters** in `font8x16`
  and 21 in `font6x8`. Check with `textWidth()` before drawing anything whose
  length you do not control.

Centring a big number:

```cpp
const uint16_t width = HSSD1306<128, 64>::textWidth("24.5", HFonts::font24x32);
oled.drawText((128 - width) / 2, 20, "24.5", HFonts::font24x32);
```

### Adding a font

A concrete font supplies glyph tables and the codepoint blocks they cover, and
writes no lookup code at all:

```cpp
class HFont5x7 : public HFont<5, 7> {
 public:
  HFont5x7();
};

// in the .cpp
const HFontRange kRanges[] = {
    {0x20, 95, kLatin},       // ' ' .. '~'
    {0x0410, 64, kCyrillic},  // 'А' .. 'я'
};
HFont5x7::HFont5x7() : HFont<5, 7>(kRanges, 2) {}
```

Ranges exist so a font with Latin *and* Cyrillic is affordable: the two blocks
sit 900 codepoints apart, and one flat table indexed by codepoint would spend
~5 KB on the hole between them. A range is just a pointer, so several scattered
codepoint runs can share one contiguous glyph table — which is how the eight
Ukrainian letters are stored without paying for the Serbian and Macedonian
ones sitting between them.

Glyph layout is `width × pages` bytes, page by page, each byte eight vertical
pixels with the LSB at the top — exactly how the SSD1306 stores display RAM, so
a glyph reaches the panel without being transformed.

An unmapped codepoint yields a hollow rectangle, built at construction from the
font's own dimensions. `glyph()` never returns null.

## Configuration

| Macro | Default | Meaning |
|---|---|---|
| `HSSD1306_ADDRESS` | `0x3C` | `0x3D` with the module's jumper moved. |
| `HSSD1306_I2C_SPEED_HZ` | `400000` | Fast mode; every panel of this kind supports it. |
| `HSSD1306_I2C_TIMEOUT_MS` | `100` | Per transfer. |

## Design notes

**There is a framebuffer.** The AVR original drew straight down the wire
because it had 512 bytes of RAM, which is why it needed `compose_bmp_px`,
`clear_area_px` and `send_buf`: the panel cannot be read back over I2C, so two
sprites sharing a page erased each other unless the caller composited them by
hand. All of that is gone.

**Each page carries its own `0x40` control byte inside the buffer**, so a page
can be handed to `i2c_master_transmit()` directly — no assembly buffer, no
copy, no scatter-gather.

**Page addressing mode**, not the reference's horizontal mode. This driver
flushes a page at a time, and page mode is what makes the column pointer wrap
inside one page instead of running on into the next.

**The bus is borrowed, never owned** — which is the whole reason a sensor can
share the same two wires.

Everything that does not depend on panel size lives in `HSSD1306Panel` and is
compiled once, however many sizes are instantiated.

Not thread-safe: one task owns one display.

## Troubleshooting

| Symptom | Cause |
|---|---|
| `no panel answering at 0x3C` | Try `0x3D`. Otherwise power or wiring. |
| Panel stays dark, but `begin()` succeeded | Charge pump. The init sequence sends `0x8D 0x14`; a clone may want different. |
| Text is a row of hollow boxes | The font has no glyph for those codepoints. Check the range table above. |
| Text runs off the right edge | No wrapping by design. Check `textWidth()`. |
| Top or bottom rows scrambled | `HEIGHT` does not match the panel — it sets the multiplex ratio and COM pin config. |
| Half a frame updates | A transfer failed; the dirty page retries on the next `flush()`. Check `flush()`'s return. |
