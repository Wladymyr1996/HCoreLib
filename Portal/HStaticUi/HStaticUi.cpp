#include "HStaticUi/HStaticUi.hpp"

#define HLOG_MODULE_NAME "Ui"
#include <HLog/HLog.hpp>

#include <cstdio>

#include "HNetwork/HNetwork.hpp"

namespace {

const uint8_t* pageStart = nullptr;
const uint8_t* pageEnd = nullptr;
HStaticUiEncoding pageEncoding = HStaticUiEncoding::Gzip;

/** @brief Sends the page. Both of its routes are the same handler. */
esp_err_t handlePage(httpd_req_t* request) {
  if (pageStart == nullptr || pageEnd <= pageStart) {
    // A build that forgot to embed one. Saying so beats a 404, which would
    // suggest the path was wrong rather than the firmware incomplete.
    HCritical("no page has been set - see HStaticUi::setPage()");
    httpd_resp_set_status(request, "503 Service Unavailable");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, "{\"status\":\"no page\"}", HTTPD_RESP_USE_STRLEN);
  }

  httpd_resp_set_type(request, "text/html; charset=utf-8");

  if (pageEncoding == HStaticUiEncoding::Gzip) {
    // The bytes in flash ARE the gzip stream. Saying so is the whole trick: the
    // browser inflates it, the device never does.
    httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
  }

  // Not cached. The page is a few kilobytes over one hop, and a stale copy after
  // a firmware update is a support call - a browser holding yesterday's UI
  // against today's API is exactly the confusion this mode exists to avoid.
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");

  const size_t length = static_cast<size_t>(pageEnd - pageStart);
  return httpd_resp_send(request, reinterpret_cast<const char*>(pageStart), length);
}

/**
 * @brief Sends everything else to the page, which is what raises the portal.
 *
 * A phone that has just joined a network asks for a known URL of its vendor's -
 * `/generate_204`, `/hotspot-detect.html`, `/ncsi.txt` - and decides from the
 * answer whether it has reached the internet. HCaptiveDns points those names at
 * this device; this turns the request into a 302, which the handset reads as
 * "there is a sign-in page here" and shows as a notification.
 */
esp_err_t handleRedirect(httpd_req_t* request) {
  char location[64] = "";
  std::snprintf(location, sizeof(location), "http://%s/", HNetwork::ip());

  HDebug("redirecting %s to the portal", request->uri);

  httpd_resp_set_status(request, "302 Found");
  httpd_resp_set_hdr(request, "Location", location);

  // Nothing may be cached: the next network this phone joins must be probed
  // again rather than remembered as a portal.
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");

  return httpd_resp_send(request, nullptr, 0);
}

const httpd_uri_t kRoot = {
    .uri = "/", .method = HTTP_GET, .handler = &handlePage, .user_ctx = nullptr};

const httpd_uri_t kIndex = {
    .uri = "/index.html", .method = HTTP_GET, .handler = &handlePage, .user_ctx = nullptr};

/**
 * Registered LAST of the GET routes, and after the API's own catch-all: with
 * wildcard matching the first registered pattern wins, so this one only ever
 * sees paths nothing else claimed.
 */
const httpd_uri_t kAnythingElse = {
    .uri = "/*", .method = HTTP_GET, .handler = &handleRedirect, .user_ctx = nullptr};

}  // namespace

void HStaticUi::setPage(const uint8_t* start, const uint8_t* end,
                        HStaticUiEncoding encoding) noexcept {
  pageStart = start;
  pageEnd = end;
  pageEncoding = encoding;
}

bool HStaticUi::hasPage() noexcept {
  return pageStart != nullptr && pageEnd > pageStart;
}

bool HStaticUi::registerRoutes(httpd_handle_t server) noexcept {
  if (server == nullptr) {
    return false;
  }

  if (httpd_register_uri_handler(server, &kRoot) != ESP_OK ||
      httpd_register_uri_handler(server, &kIndex) != ESP_OK ||
      httpd_register_uri_handler(server, &kAnythingElse) != ESP_OK) {
    HCritical("could not register the page routes");
    return false;
  }

  if (hasPage()) {
    HInfo("page ready: %u bytes%s", static_cast<unsigned>(pageEnd - pageStart),
          (pageEncoding == HStaticUiEncoding::Gzip) ? ", gzipped" : "");
  } else {
    HWarning("routes registered, but no page has been set");
  }

  return true;
}
