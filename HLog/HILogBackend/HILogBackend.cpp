#include "HILogBackend.hpp"

// Out-of-line destructor: anchors the vtable in this translation unit instead
// of emitting it in every file that implements or includes the interface.
HILogBackend::~HILogBackend() = default;
