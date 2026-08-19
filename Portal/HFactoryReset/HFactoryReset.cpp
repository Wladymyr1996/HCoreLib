#include "HFactoryReset/HFactoryReset.hpp"

#define HLOG_MODULE_NAME "Factory"
#include <HLog/HLog.hpp>

#include <HBootMode/HBootMode.hpp>
#include <HConfig/HConfig.hpp>
#include <HHookList/HHookList.hpp>
#include <HSystemUtils/HSystemUtils.hpp>



namespace {

/**
 * @brief Set by the API, read by MainTask.
 *
 * A plain bool rather than a mutex-guarded one: it is written once, by one task,
 * and only ever read as "has this become true". The worst a race can do is
 * notice it one tick later.
 */
volatile bool requested = false;

}  // namespace

HHookList<HFACTORYRESET_MAX_HOOKS>& HFactoryReset::onErase() noexcept {
  static HHookList<HFACTORYRESET_MAX_HOOKS> hooks;
  return hooks;
}

void HFactoryReset::request() noexcept {
  HWarning("factory reset requested");
  requested = true;
}

bool HFactoryReset::isRequested() noexcept {
  return requested;
}

void HFactoryReset::perform() noexcept {
  HWarning("erasing every stored setting");

  if (!HConfig::removeAll()) {
    // Worth saying, not worth stopping for. Whatever could not be deleted is
    // reported by HConfig itself, and coming back up on a partly-cleared
    // configuration is still better than sitting in a mode that does nothing.
    HCritical("some configuration could not be erased");
  }

  // The files are gone. Whatever else this device keeps in RTC memory would
  // otherwise hand the old values straight back on the next boot, and the reset
  // would look like it had not happened - so the application clears its own
  // caches here, through the hook.


  onErase().invoke();

  HInfo("done - restarting into Normal");

  // Long enough for the console to say so before the reset lands.
  HSystemUtils::sleep(50);

  HBootMode::rebootTo(HBootModeKind::Normal);
}
