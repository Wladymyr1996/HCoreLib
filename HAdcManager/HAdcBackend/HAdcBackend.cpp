#include "HAdcBackend.hpp"

#include <HCoreLib.h>

#if IS_MCU
#include "../HAdcEsp32/HAdcEsp32.hpp"
#else
#include "../HAdcDesktop/HAdcDesktop.hpp"
#endif

HIAdc& hAdcBackend() noexcept {
  // A function-local static rather than a namespace-scope object: a channel
  // read from another translation unit's static constructor would otherwise
  // race this object's own construction, and static init order across TUs is
  // not defined.
#if IS_MCU
  static HAdcEsp32 adc;
#else
  static HAdcDesktop adc;
#endif
  return adc;
}
