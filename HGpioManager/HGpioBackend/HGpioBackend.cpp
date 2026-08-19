#include "HGpioBackend.hpp"

#include <HCoreLib.h>

#if IS_MCU
#include "../HGpioEsp32/HGpioEsp32.hpp"
#else
#include "../HGpioDesktop/HGpioDesktop.hpp"
#endif

HIGpio& hGpioBackend() noexcept {
  // A function-local static rather than a namespace-scope object: a pin read
  // from another translation unit's static constructor would otherwise race
  // this object's own construction, and static init order across TUs is not
  // defined.
#if IS_MCU
  static HGpioEsp32 gpio;
#else
  static HGpioDesktop gpio;
#endif
  return gpio;
}
