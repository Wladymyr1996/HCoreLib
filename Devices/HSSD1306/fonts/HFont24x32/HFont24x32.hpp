#pragma once

#include "../../HFont/HFont.hpp"

/**
 * @brief 24x32 font: Comic Sans, digits and symbols only.
 *
 * Four pages tall (32 pixels = 4 * 8), twenty-four columns per glyph, spacing
 * included. Covers:
 *
 * | Block | Codepoints | State |
 * | --- | --- | --- |
 * | ASCII | ' ' (0x20) .. '9' (0x39) | drawn |
 *
 * All the lookup logic is HFont's. This class exists only to name the tables.
 */
class HFont24x32 : public HFont<24, 32> {
 public:
  HFont24x32();
};

/** @brief Ready-made font instances, so callers need not build their own. */
namespace HFonts {

/** @brief The shared 24x32 font. */
extern const HFont24x32 font24x32;

}  // namespace HFonts
