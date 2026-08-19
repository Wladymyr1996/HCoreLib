#include "HSleep.hpp"

#define HLOG_MODULE_NAME "HSleep"
#include <HLog/HLog.hpp>

#if IS_MCU

#include <driver/rtc_io.h>
#include <esp_sleep.h>

#else

#include <cstdlib>

#endif

namespace {

/**
 * @brief The two hook lists.
 *
 * Function-local statics so an application registering from a static
 * constructor cannot race their construction.
 */
HHookList<HSLEEP_MAX_HOOKS>& beforeSleepHooks() noexcept {
  static HHookList<HSLEEP_MAX_HOOKS> hooks;
  return hooks;
}

HHookList<HSLEEP_MAX_HOOKS>& afterWakeHooks() noexcept {
  static HHookList<HSLEEP_MAX_HOOKS> hooks;
  return hooks;
}

}  // namespace

HHookList<HSLEEP_MAX_HOOKS>& HSleep::onBeforeSleep() noexcept {
  return beforeSleepHooks();
}

HHookList<HSLEEP_MAX_HOOKS>& HSleep::onAfterWake() noexcept {
  return afterWakeHooks();
}

void HSleep::notifyWake() noexcept {
  if (wakeCause() == HWakeCause::ColdBoot) {
    return;
  }

  afterWakeHooks().invoke();
}

HWakeCause HSleep::wakeCause() noexcept {
#if IS_MCU
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return HWakeCause::Timer;

    // Two names for one intent: esp_deep_sleep_enable_gpio_wakeup() reports
    // GPIO, the EXT1 source reports EXT1, and to a caller both mean "a pin the
    // device armed went active".
    case ESP_SLEEP_WAKEUP_GPIO:
    case ESP_SLEEP_WAKEUP_EXT1:
      return HWakeCause::Button;

    case ESP_SLEEP_WAKEUP_UNDEFINED:
      // Not a wake at all: power-on, reset button, brownout, crash.
      return HWakeCause::ColdBoot;

    default:
      return HWakeCause::Other;
  }
#else
  // A host process is always a cold start; there is nothing to wake from.
  return HWakeCause::ColdBoot;
#endif
}

bool HSleep::enableTimerWakeup(uint32_t ms) noexcept {
#if IS_MCU
  const esp_err_t result = esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(ms) * 1000ULL);
  if (result != ESP_OK) {
    HCritical("timer wakeup (%u ms) rejected: %s", static_cast<unsigned>(ms),
              esp_err_to_name(result));
    return false;
  }
  return true;
#else
  (void)ms;
  return true;
#endif
}

bool HSleep::enableButtonWakeup(const HGpioPin& pin) noexcept {
  if (!pin.isValid()) {
    HCritical("button wakeup asked for on a pin that is not declared");
    return false;
  }

#if IS_MCU
  const gpio_num_t number = static_cast<gpio_num_t>(pin.number());

  if (!rtc_gpio_is_valid_gpio(number)) {
    HCritical("GPIO%d is not in the low-power domain and cannot wake the chip", pin.number());
    return false;
  }

  // A pull in the RTC domain, because the digital pad control is powered down
  // in deep sleep. Without it the input floats and the device wakes instantly,
  // over and over, until the battery is flat.
  rtc_gpio_pullup_dis(number);
  rtc_gpio_pulldown_dis(number);

  esp_deepsleep_gpio_wake_up_mode_t mode = ESP_GPIO_WAKEUP_GPIO_HIGH;
  if (pin.isActiveLow()) {
    // Switch to ground: hold the pad HIGH so that only the press pulls it down.
    rtc_gpio_pullup_en(number);
    mode = ESP_GPIO_WAKEUP_GPIO_LOW;
  } else {
    rtc_gpio_pulldown_en(number);
  }

  const esp_err_t result = esp_deep_sleep_enable_gpio_wakeup(1ULL << pin.number(), mode);
  if (result != ESP_OK) {
    HCritical("GPIO%d wakeup rejected: %s", pin.number(), esp_err_to_name(result));
    return false;
  }

  HDebug("%s (GPIO%d) will wake the chip on %s", pin.name(), pin.number(),
         (mode == ESP_GPIO_WAKEUP_GPIO_LOW) ? "LOW" : "HIGH");
  return true;
#else
  return true;
#endif
}

void HSleep::deepSleep() noexcept {
  // The application's last word before the chip stops - see onBeforeSleep().
  // Run here rather than left to the caller, so no path to sleep can skip them.
  beforeSleepHooks().invoke();

#if IS_MCU
  esp_deep_sleep_start();
#else
  // A host process cannot sleep and come back as a fresh boot. Exiting is the
  // closest honest equivalent - and the App this serves is target-only anyway.
  std::exit(0);
#endif
}
