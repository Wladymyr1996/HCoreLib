#include "HRtcStore.hpp"

#define HLOG_MODULE_NAME "Rtc"
#include <HLog/HLog.hpp>

#include <etl/vector.h>

namespace {

using Registry = etl::vector<uint32_t*, HRTCSTORE_MAX_BLOCKS>;

/**
 * @brief The tracked blocks.
 *
 * Function-local so a module registering from a static constructor cannot race
 * the registry's own construction. It holds POINTERS into RTC memory, and lives
 * in ordinary RAM itself - it is rebuilt on every boot by the init() calls, and
 * has nothing worth retaining.
 */
Registry& registry() noexcept {
  static Registry blocks;
  return blocks;
}

}  // namespace

void HRtcStore::track(uint32_t& magic) noexcept {
  Registry& blocks = registry();

  for (uint32_t* tracked : blocks) {
    if (tracked == &magic) {
      return;
    }
  }

  if (blocks.full()) {
    // Not fatal, but it means a cold restart would leave this block vouching
    // for itself - which is exactly the state the restart exists to remove.
    HCritical("no room to track another retained block - raise HRTCSTORE_MAX_BLOCKS");
    return;
  }

  blocks.push_back(&magic);
}

bool HRtcStore::isValid(uint32_t magic, uint32_t expected) noexcept {
  return magic == expected;
}

void HRtcStore::invalidateAll() noexcept {
  for (uint32_t* magic : registry()) {
    *magic = 0;
  }
}

size_t HRtcStore::trackedCount() noexcept {
  return registry().size();
}
