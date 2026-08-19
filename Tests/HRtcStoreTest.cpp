#include "HCoreLibTest.hpp"

#include <cstdint>

#include <HRtcStore/HRtcStore.hpp>

/**
 * @file HRtcStoreTest.cpp
 * @brief The magic-word dance, and the registry that can undo it.
 *
 * There is no RTC memory on a desktop, and it does not matter: what this class
 * actually is, is a registry of `uint32_t&` plus one comparison. The behaviour
 * worth pinning is that invalidateAll() reaches EVERY tracked block - because
 * HTaskManager restarting a hung device "cold" depends on the next boot finding
 * no stale state vouching for itself, and a block this class missed would be
 * exactly that.
 */

namespace {

constexpr uint32_t kMagicA = 0xA5A5A5A5U;
constexpr uint32_t kMagicB = 0x5A5A5A5AU;

void checkValidity() {
  uint32_t block = 0;

  // Uninitialised RTC memory is whatever the SRAM powered up holding, so a
  // block cannot be believed until it says the right thing.
  CHECK(!HRtcStore::isValid(block, kMagicA));

  block = kMagicA;
  CHECK(HRtcStore::isValid(block, kMagicA));

  // Each block needs its own word: two blocks sharing one would each vouch
  // for the other's garbage.
  CHECK(!HRtcStore::isValid(block, kMagicB));
}

void checkTracking() {
  const size_t before = HRtcStore::trackedCount();

  // Static, because the registry keeps the REFERENCE - exactly as a real
  // caller's RTC_DATA_ATTR block would outlive the init() that tracked it.
  // A stack local here would leave the registry pointing at a dead frame.
  static uint32_t first = kMagicA;
  static uint32_t second = kMagicB;

  HRtcStore::track(first);
  CHECK(HRtcStore::trackedCount() == before + 1);

  // Registering the same word twice is harmless and does not consume a slot -
  // an init() called twice must not cost the registry a place.
  HRtcStore::track(first);
  CHECK(HRtcStore::trackedCount() == before + 1);

  HRtcStore::track(second);
  CHECK(HRtcStore::trackedCount() == before + 2);

  HRtcStore::invalidateAll();
  CHECK(first == 0);
  CHECK(second == 0);
  CHECK(!HRtcStore::isValid(first, kMagicA));
  CHECK(!HRtcStore::isValid(second, kMagicB));

  // Only the proof goes; the registry itself survives, so a block re-seeded
  // after a fault is still covered by the next invalidateAll().
  CHECK(HRtcStore::trackedCount() == before + 2);
  first = kMagicA;
  HRtcStore::invalidateAll();
  CHECK(first == 0);
}

/** @brief A full registry refuses quietly rather than writing past its end. */
void checkCapacity() {
  static uint32_t blocks[HRTCSTORE_MAX_BLOCKS + 2];

  for (size_t i = 0; i < HRTCSTORE_MAX_BLOCKS + 2; ++i) {
    blocks[i] = kMagicA;
    HRtcStore::track(blocks[i]);
  }
  CHECK(HRtcStore::trackedCount() == HRTCSTORE_MAX_BLOCKS);

  HRtcStore::invalidateAll();
  CHECK(blocks[0] == 0);
}

}  // namespace

void runRtcStoreTests() noexcept {
  HCoreLibTest::begin("HRtcStore");

  checkValidity();
  checkTracking();
  checkCapacity();
}
