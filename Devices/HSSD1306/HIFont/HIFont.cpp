#include "HIFont.hpp"

// Out-of-line, and that is the whole reason this file exists: it anchors the
// vtable in one translation unit instead of emitting it in every file that
// includes the header. See CodeStyle.md.
HIFont::~HIFont() {
}
