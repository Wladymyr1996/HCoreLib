#include "HITickable.hpp"

// Out-of-line destructor: anchors the vtable in this translation unit instead
// of emitting it in every file that implements the interface.
HITickable::~HITickable() = default;
