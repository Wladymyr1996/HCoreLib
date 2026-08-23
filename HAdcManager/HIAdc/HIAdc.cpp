#include "HIAdc.hpp"

// Out of line on purpose: it anchors the vtable in this one translation unit
// instead of emitting it in every file that includes the header.
HIAdc::~HIAdc() = default;
