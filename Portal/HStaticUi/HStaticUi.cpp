#include "HStaticUi/HStaticUi.hpp"

#define HLOG_MODULE_NAME "Ui"
#include <HLog/HLog.hpp>

#include <cstdio>

#include "HNetwork/HNetwork.hpp"

namespace {

/*
 * The favicon, embedded by this component's CMakeLists.
 *
 * Not settable, unlike the page: every device in the ecosystem is the same
 * product to a browser, so the icon on the tab is the library's and there is no
 * seam here for an application to reach through. See the class documentation.
 */
extern "C" const uint8_t kFaviconStart[] asm("_binary_hcore_favicon_png_start");
extern "C" const uint8_t kFaviconEnd[] asm("_binary_hcore_favicon_png_end");

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
 * @brief Sends the icon the browser puts on its tab.
 *
 * Cached, unlike everything else this server sends. The page is `no-store`
 * because a stale UI against a fresh API is a support call; the icon cannot be
 * stale in any way that matters - it changes only with a firmware update, and a
 * day-old copy of it is still the right picture. Refetching under a kilobyte on
 * every page load is radio time an access point running off a battery pays for.
 */
esp_err_t handleFavicon(httpd_req_t* request) {
  const size_t length = static_cast<size_t>(kFaviconEnd - kFaviconStart);

  httpd_resp_set_type(request, "image/png");
  httpd_resp_set_hdr(request, "Cache-Control", "max-age=86400");

  return httpd_resp_send(request, reinterpret_cast<const char*>(kFaviconStart), length);
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

/*
 * Both spellings, and the `.ico` one matters most: a browser asks for
 * `/favicon.ico` on its own, without being told to by any markup, which is what
 * makes the icon appear on a page that never mentions it. PNG bytes under that
 * name are read correctly by every browser this device will meet - the
 * Content-Type says what they are, and the extension is only a habit.
 *
 * Both must be registered BEFORE the catch-all below, or the redirect swallows
 * them and the browser is handed the page where it expected a picture.
 */
const httpd_uri_t kFaviconIco = {
    .uri = "/favicon.ico", .method = HTTP_GET, .handler = &handleFavicon, .user_ctx = nullptr};

const httpd_uri_t kFaviconPng = {
    .uri = "/favicon.png", .method = HTTP_GET, .handler = &handleFavicon, .user_ctx = nullptr};

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
      httpd_register_uri_handler(server, &kFaviconIco) != ESP_OK ||
      httpd_register_uri_handler(server, &kFaviconPng) != ESP_OK ||
      httpd_register_uri_handler(server, &kAnythingElse) != ESP_OK) {
    // Nearly always the route budget: raise HWEBSERVER_MAX_ROUTES. Loud,
    // because a portal whose catch-all never registered still serves its page
    // and still looks fine - it just stops raising the sign-in prompt that
    // makes anybody open it.
    HCritical("could not register the page routes - is HWEBSERVER_MAX_ROUTES big enough?");
    return false;
  }

  if (hasPage()) {
    HInfo("page ready: %u bytes%s, favicon %u bytes",
          static_cast<unsigned>(pageEnd - pageStart),
          (pageEncoding == HStaticUiEncoding::Gzip) ? ", gzipped" : "",
          static_cast<unsigned>(kFaviconEnd - kFaviconStart));
  } else {
    HWarning("routes registered, but no page has been set");
  }

  return true;
}
