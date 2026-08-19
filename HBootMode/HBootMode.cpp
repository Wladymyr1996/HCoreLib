#include "HBootMode.hpp"

#define HLOG_MODULE_NAME "HBootMode"
#include <HLog/HLog.hpp>

#if IS_MCU

#include <esp_attr.h>
#include <esp_system.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#else

#include <cstdlib>

#endif

namespace {

/**
 * @brief Marks the RTC block as written by THIS firmware, not left by SRAM noise.
 *
 * Arbitrary but non-trivial: a word of all zeroes or all ones is exactly what an
 * uninitialised or a stuck block is most likely to read as.
 */
const uint32_t kRequestMagic = 0x48544850u;  // "HTHP"

/**
 * @brief The highest defined mode. The ONE place the enum's extent is written down.
 *
 * isValid() range-checks against it, so a mode added to the header without
 * touching this is a mode that cannot be reached: its request is discarded as
 * out of range and the node quietly boots Normal.
 */
const uint8_t kLastMode = static_cast<uint8_t>(HBootModeKind::FactoryReset);

#if IS_MCU

/**
 * @brief The one-shot request, in RTC slow memory.
 *
 * RTC_NOINIT_ATTR rather than RTC_DATA_ATTR, and the difference is the whole
 * mechanism: RTC_DATA_ATTR is re-initialised from the image on every reset,
 * which would wipe the request before anything could read it. NOINIT is left
 * exactly as it was - across a software reset and across deep sleep, but NOT
 * across a power cycle, where it comes up holding whatever the SRAM powered up
 * with. That is what kRequestMagic is checked against.
 */
RTC_NOINIT_ATTR uint32_t rtcMagic;
RTC_NOINIT_ATTR uint32_t rtcMode;

#else

// Desktop has no RTC domain and no reset to survive. Plain statics keep the
// code path identical and the request simply does not outlive the process,
// which is the honest equivalent of a power cycle.
uint32_t rtcMagic = 0;
uint32_t rtcMode = 0;

#endif

}  // namespace

HBootModeKind HBootMode::current_ = HBootModeKind::Normal;
bool HBootMode::resolved_ = false;

bool HBootMode::isValid(uint8_t raw) noexcept {
  return raw <= kLastMode;
}

const char* HBootMode::name(HBootModeKind mode) noexcept {
  switch (mode) {
    case HBootModeKind::Normal:
      return "Normal";
    case HBootModeKind::Configuring:
      return "Configuring";
    case HBootModeKind::Ota:
      return "Ota";
    case HBootModeKind::FactoryReset:
      return "FactoryReset";
  }
  // Unreachable for a value that came from resolve(), which range-checks
  // everything it accepts. Present because a switch over an enum class with no
  // default is a warning waiting to happen on a future compiler.
  return "Normal";
}

bool HBootMode::takeRequest(HBootModeKind& out) noexcept {
  if (rtcMagic != kRequestMagic) {
    // A cold boot, or a firmware that never wrote one. Not an error - it is the
    // usual case, and Normal is the answer from here.
    return false;
  }

  // CONSUMED before the value is even validated, and before anything can fail.
  // A request that survived into a second boot would turn one bad decision into
  // a node permanently stuck in a mode that does not do the job.
  rtcMagic = 0;

  const uint32_t raw = rtcMode;
  if (!isValid(static_cast<uint8_t>(raw))) {
    HWarning("RTC held an out-of-range boot mode (%u) - ignoring it", static_cast<unsigned>(raw));
    return false;
  }

  out = static_cast<HBootModeKind>(static_cast<uint8_t>(raw));
  return true;
}

HBootModeKind HBootMode::resolve() noexcept {
  if (resolved_) {
    return current_;
  }

  HBootModeKind mode = HBootModeKind::Normal;

  if (takeRequest(mode)) {
    HInfo("boot mode %s (one-shot request from RTC)", name(mode));
  } else {
    // No sticky default to consult yet - see the "what is not here yet" note in
    // the header. Normal is the safe answer either way: it is the only mode that
    // actually does the device's job.
    HInfo("boot mode %s (no pending request)", name(mode));
  }

  current_ = mode;
  resolved_ = true;
  return current_;
}

HBootModeKind HBootMode::current() noexcept {
  if (!resolved_) {
    return resolve();
  }
  return current_;
}

void HBootMode::requestOnce(HBootModeKind mode) noexcept {
  // Mode first, magic second. If power is lost between the two stores the block
  // reads as invalid and the node comes up Normal - whereas the other order
  // would leave a valid magic vouching for a stale mode.
  rtcMode = static_cast<uint32_t>(static_cast<uint8_t>(mode));
  rtcMagic = kRequestMagic;
  HInfo("next boot will be %s", name(mode));
}

void HBootMode::clearRequest() noexcept {
  // The magic alone: the mode word can stay as it is, because without a valid
  // magic vouching for it nothing will ever read it.
  rtcMagic = 0;
}

void HBootMode::rebootTo(HBootModeKind mode) noexcept {
  requestOnce(mode);
  HInfo("rebooting into %s", name(mode));

#if IS_MCU
  // Long enough for the console UART to drain: HLog writes straight to it with
  // no buffer of its own, and the peripheral's FIFO is still shifting out when
  // the reset lands.
  vTaskDelay(pdMS_TO_TICKS(HBOOTMODE_UART_DRAIN_MS));

  esp_restart();
#else
  // Desktop has no reset to perform. Exiting is the closest honest equivalent:
  // the process starts over from main() with every cached value re-read, which
  // is what the caller asked for. The one-shot request does not survive it, the
  // same way it would not survive a power cycle on the target.
  std::exit(0);
#endif
}
