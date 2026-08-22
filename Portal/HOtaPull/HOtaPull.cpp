#include "HOtaPull/HOtaPull.hpp"

#define HLOG_MODULE_NAME "OtaPull"
#include <HLog/HLog.hpp>

#include <esp_http_client.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <HTimer/HTimer.hpp>

#include "HNetwork/HNetwork.hpp"
#include "HOtaWriter/HOtaWriter.hpp"

namespace {

HOtaProgressHook progressHook;

/** Static: this runs on a task whose stack also carries esp_ota and lwIP. */
uint8_t chunk[HOTAPULL_CHUNK_SIZE];

/** @brief Tells the caller how far along this is, so it can feed its watchdog. */
void reportProgress(size_t received, size_t total) noexcept {
  if (progressHook.is_valid()) {
    progressHook(received, total);
  }
}

/**
 * @brief Waits for the router to hand over an address.
 *
 * @return false on the timeout, which is the honest answer for a passphrase
 *         that is wrong: the radio retries forever by design (see HNetwork's
 *         event handler) and nothing below it will ever say "no".
 */
bool waitForAddress(HTimer& overall) noexcept {
  HTimer joining(HOTAPULL_JOIN_TIMEOUT_MS);
  joining.start();

  while (HNetwork::status() != HNetworkStatus::Connected) {
    if (joining.isExpired() || overall.isExpired()) {
      HCritical("no address after %u ms - wrong passphrase, or nothing answering",
                static_cast<unsigned>(HOTAPULL_JOIN_TIMEOUT_MS));
      return false;
    }

    // Through the hook, so the caller's watchdog is fed while this waits.
    reportProgress(0, 0);
    vTaskDelay(pdMS_TO_TICKS(HCORELIB_TICK_MS));
  }

  HInfo("joined as %s", HNetwork::ip());
  return true;
}

}  // namespace

void HOtaPull::onProgress(HOtaProgressHook hook) noexcept {
  progressHook = hook;
}

HOtaPullResult HOtaPull::perform(const char* ssid, const char* passphrase, uint8_t channel,
                                 const char* url, bool allowSameVersion) noexcept {
  HTimer overall(HOTAPULL_TIMEOUT_MS);
  overall.start();

  if (!HNetwork::startStation(ssid, passphrase, channel)) {
    return HOtaPullResult::NoNetwork;
  }

  if (!waitForAddress(overall)) {
    return HOtaPullResult::NoNetwork;
  }

  esp_http_client_config_t config = {};
  config.url = url;
  config.timeout_ms = HOTAPULL_HTTP_TIMEOUT_MS;
  config.disable_auto_redirect = false;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    HCritical("could not build a client for %s", url);
    return HOtaPullResult::NoImage;
  }

  HOtaPullResult outcome = HOtaPullResult::NoImage;

  do {
    if (esp_http_client_open(client, 0) != ESP_OK) {
      HCritical("could not reach %s", url);
      break;
    }

    // May be -1 for a chunked response, which is legal and simply means the
    // total is not known. HOtaWriter skips its size check then rather than
    // guessing, and esp_ota_write() still refuses honestly if the slot fills.
    const int64_t announced = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);

    if (status != 200) {
      HCritical("%s answered %d", url, status);
      break;
    }

    const size_t total = (announced > 0) ? static_cast<size_t>(announced) : 0;
    HInfo("downloading %u bytes from %s", static_cast<unsigned>(total), url);

    // Downgrades are refused on this path whatever the master said: an image
    // pushed over the air lands on a node nobody is looking at, and there is
    // nobody there to agree to it. Reinstalling the SAME version is the
    // master's to grant, and it has to grant it explicitly - otherwise a
    // master could re-push one image to a battery node every hour without
    // meaning to.
    if (!HOtaWriter::begin(total, false, allowSameVersion)) {
      outcome = HOtaPullResult::Refused;
      break;
    }

    size_t received = 0;
    bool failed = false;

    for (;;) {
      if (overall.isExpired()) {
        HCritical("gave up after %u ms with %u bytes",
                  static_cast<unsigned>(HOTAPULL_TIMEOUT_MS),
                  static_cast<unsigned>(received));
        HOtaWriter::abort();
        outcome = HOtaPullResult::TimedOut;
        failed = true;
        break;
      }

      const int read = esp_http_client_read(client, reinterpret_cast<char*>(chunk),
                                            sizeof(chunk));
      if (read < 0) {
        HCritical("the connection failed after %u bytes", static_cast<unsigned>(received));
        HOtaWriter::abort();
        failed = true;
        break;
      }

      if (read == 0) {
        // Either the image ended or the socket did. esp_ota_end() inside
        // finish() tells the two apart: a truncated image fails its SHA.
        break;
      }

      if (!HOtaWriter::write(chunk, static_cast<size_t>(read))) {
        outcome = HOtaPullResult::Refused;
        failed = true;
        break;
      }

      received += static_cast<size_t>(read);

      // Between chunks, which is what keeps a caller's watchdog fed through a
      // download far longer than any watchdog allows.
      reportProgress(received, total);
    }

    if (failed) {
      break;
    }

    outcome = HOtaWriter::finish() ? HOtaPullResult::Installed : HOtaPullResult::Refused;
  } while (false);

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  // The radio has done its job either way, and this mode is about to reboot.
  // Stopping it first means the reset happens with the PA off rather than mid
  // transmission.
  HNetwork::stop();

  return outcome;
}

const char* HOtaPull::resultText(HOtaPullResult result) noexcept {
  switch (result) {
    case HOtaPullResult::Installed:
      return "installed";
    case HOtaPullResult::NoNetwork:
      return "no network";
    case HOtaPullResult::NoImage:
      return "no image";
    case HOtaPullResult::Refused:
      return "refused";
    case HOtaPullResult::TimedOut:
      return "timed out";
  }

  return "unknown";
}
