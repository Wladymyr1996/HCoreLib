#pragma once

#include <esp_http_server.h>

#include <HCoreLib.h>

#include "HRestApi/HRestRequest/HRestRequest.hpp"
#include "HRestApi/HRestResponse/HRestResponse.hpp"
#include "HRestApi/HRestTypes/HRestTypes.hpp"

/** How many routes an application may add on top of the library's own. */
#ifndef HRESTAPI_MAX_ROUTES
#define HRESTAPI_MAX_ROUTES 8
#endif

/**
 * @brief The REST API every Hatynka device shares, plus a way in for its own.
 *
 * ## What the library serves
 * | Route | Key | Answers |
 * | --- | --- | --- |
 * | `GET /api/auth` | no | `{"status":"first"\|"unathorized"\|"ok"}` |
 * | `POST /api/auth` | no | `{"auth_key":"…"}` or 401 |
 * | `POST /api/setAdminPassword` | yes* | `{"status":"ok"}` |
 * | `POST /api/factoryReset` | yes | `{"status":"ok"}`, then the device restarts |
 * | `GET /api/info` | no | `{"name","fw","idf"}` |
 * | `OPTIONS` on any path | no | the CORS preflight every browser sends first |
 * | `GET`/`POST` under `/api` | no | 404, so a mistyped API path answers as the API |
 *
 * \* open until an admin password exists - see HAuth.
 *
 * These are identical on every device, which is the whole reason they are here:
 * a thermometer and a door controller authenticate the same way, report their
 * firmware the same way, and are reset the same way.
 *
 * ## What the application adds
 * Everything about a particular device - what it measures, what it can be told:
 *
 * @code
 *   HRestApi::begin(server);                       // the routes above
 *   HRestApi::add(HRestMethod::Get,  "/api/values", onValues, HRestAuth::None);
 *   HRestApi::add(HRestMethod::Post, "/api/config", onConfig, HRestAuth::Required);
 *   HRestApi::finish();                            // the catch-alls, last
 * @endcode
 *
 * **The auth flag is enforced here**, before a handler runs. That is the point
 * of taking it as an argument rather than leaving each handler to check: an
 * application route cannot forget to guard itself, and a reviewer can see what
 * every route requires in one place.
 *
 * ## Order matters, which is why there are three calls
 * The server matches routes in registration order, so anything with a wildcard
 * has to come last. begin() registers the exact library paths, add() the
 * application's, and finish() the catch-alls - call them in that order and the
 * question never arises again.
 */
class HRestApi {
 public:
  HRestApi() = delete;

  /**
   * @brief Registers the library's own routes on a running server.
   * @return false if the server is not running or a route was rejected.
   */
  static bool begin(httpd_handle_t server) noexcept;

  /**
   * @brief Adds an application route.
   *
   * @param method GET or POST.
   * @param uri Exact path, e.g. "/api/values". Must be a string that outlives
   *        the server - a literal, in practice.
   * @param handler Called with the request and the response it must send.
   * @param auth HRestAuth::Required rejects the request with 401 before the
   *        handler is reached.
   * @return false when the table is full (raise HRESTAPI_MAX_ROUTES) or the
   *         server rejected the path.
   */
  static bool add(HRestMethod method, const char* uri, const HRestHandler& handler,
                  HRestAuth auth) noexcept;

  /**
   * @brief Registers the catch-alls. Call AFTER every add().
   *
   * The `/api` 404 and the OPTIONS preflight. Both are wildcards, so both must
   * be last.
   */
  static bool finish() noexcept;
};
