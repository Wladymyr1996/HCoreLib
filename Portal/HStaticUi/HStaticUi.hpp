#pragma once

#include <cstddef>
#include <cstdint>

#include <esp_http_server.h>

#include <HCoreLib.h>

/**
 * @brief Serves the configuration page, and sends everything else to it.
 *
 * The mechanism is shared; **the page is not**. Every device in the ecosystem
 * has a settings mode, and every one of them has its own interface to put in it,
 * so the application hands it a page and this decides how it reaches a browser.
 *
 * The one exception is the favicon, and it is the exception for the opposite
 * reason: to a browser every device in the ecosystem is the same product, and
 * the icon on the tab says so. Those bytes are the library's, embedded by this
 * component and served from `/favicon.ico` and `/favicon.png` - there is no
 * setter, and an application has nothing to register. See *The favicon* below.
 *
 * @code
 *   extern const uint8_t kStart[] asm("_binary_webui_html_gz_start");
 *   extern const uint8_t kEnd[]   asm("_binary_webui_html_gz_end");
 *
 *   HStaticUi::setPage(kStart, kEnd, HStaticUiEncoding::Gzip);
 *   HStaticUi::registerRoutes(server);
 * @endcode
 *
 * ## Gzip is stored and sent, never expanded
 * A compressed page is served AS IT IS with `Content-Encoding: gzip` - a
 * quarter of the flash, a quarter of the radio time, and no CPU at all, because
 * the browser was always going to do that work. `HCoreLib/tools/packui.py`
 * produces the file; an application's build embeds it.
 *
 * ## The favicon is the library's, and cannot be replaced
 * `HCoreLib/Portal/HStaticUi/favicon.png` is embedded into this component and
 * answered at both `/favicon.ico` and `/favicon.png`. The `.ico` spelling is the
 * one that matters: a browser asks for it unprompted, so the icon appears on a
 * page whose markup never mentions it - including one an application wrote
 * before this existed.
 *
 * Both are registered ahead of the catch-all, or the redirect below would
 * swallow them and hand a browser the page where it expected a picture. Unlike
 * the page they are sent with a `Cache-Control` that permits caching: the icon
 * changes only with a firmware update, and a day-old copy is still right.
 *
 * ## The redirect is what makes a captive portal appear
 * Every unclaimed GET is answered with a 302 to the page. A phone that has just
 * joined a network asks for a known URL of its vendor's, and reads a redirect as
 * "there is a sign-in page here" - which is the notification the owner taps. A
 * 200 with HTML can instead be taken as proof the internet works, closing the
 * very prompt it should open, so a redirect it is.
 */
enum class HStaticUiEncoding : uint8_t {
  /** Plain HTML: sent as it is. */
  Identity,

  /** A gzip stream: sent as it is, with the header that says so. */
  Gzip,
};

class HStaticUi {
 public:
  HStaticUi() = delete;

  /**
   * @brief Hands over the page this device serves.
   *
   * @param start First byte, usually a linker symbol from an embedded file.
   * @param end One past the last byte.
   * @param encoding Gzip when the bytes are compressed - the usual case.
   *
   * Nothing is copied: the range must live as long as the server, which for
   * embedded data means the life of the firmware.
   */
  static void setPage(const uint8_t* start, const uint8_t* end,
                      HStaticUiEncoding encoding = HStaticUiEncoding::Gzip) noexcept;

  /** @brief True once a page has been set. */
  static bool hasPage() noexcept;

  /**
   * @brief Registers `/`, `/index.html`, both favicon paths and the catch-all.
   *
   * Call AFTER HRestApi::finish(), so the API's own paths are claimed first:
   * registration order is matching order, and this one ends in a wildcard.
   *
   * With no page set, `/` answers 503 rather than pretending - a device whose
   * build forgot to embed one should say so, not 404 as though the path were
   * wrong. The favicon is always there either way: it is the library's.
   *
   * @return false if a route could not be registered, which nearly always means
   *         HWEBSERVER_MAX_ROUTES is too small for the routes this build wants.
   */
  static bool registerRoutes(httpd_handle_t server) noexcept;
};
