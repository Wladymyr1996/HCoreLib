#include "HFs/HIFile.hpp"

// Defined out-of-line on purpose: an anchor for the vtable, so it is emitted
// in this translation unit alone rather than in every one that includes the
// header.
HIFile::~HIFile() {
}
