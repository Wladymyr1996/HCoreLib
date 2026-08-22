#pragma once

#include <esp_http_server.h>

#include <HCoreLib.h>

/** Port the REST API and the configuration page answer on. */
#ifndef HWEBSERVER_PORT
#define HWEBSERVER_PORT 80
#endif

/**
 * Routes the server will hold. The library registers about eight of its own;
 * the rest is room for an application's.
 */
#ifndef HWEBSERVER_MAX_ROUTES
#define HWEBSERVER_MAX_ROUTES 16
#endif

/**
 * Stack for the server's own task, in bytes, where every REST handler runs.
 *
 * IDF's default is 4096, which is comfortable for handlers that parse a small
 * JSON body and answer. It is NOT comfortable for the firmware upload, which
 * writes to flash from this task and calls into esp_ota - so this is 8192 and
 * the upload's receive buffer is static rather than a local, because the two
 * together would overrun either figure.
 */
#ifndef HWEBSERVER_STACK_SIZE
#define HWEBSERVER_STACK_SIZE 8192
#endif

/**
 * How long a receive may stall before the socket is given up on, in seconds.
 *
 * IDF's default is 5. A firmware upload is a megabyte over an access point that
 * a person is holding, and a phone that briefly wanders behind a wall must not
 * cost the whole transfer - so this is longer, and the cost is that a genuinely
 * dead socket is held that much longer before it is released.
 */
#ifndef HWEBSERVER_RECV_TIMEOUT_S
#define HWEBSERVER_RECV_TIMEOUT_S 15
#endif

/**
 * @brief The HTTP server: its lifetime, and nothing about what it serves.
 *
 * esp_http_server runs a task of its own, so this class is start and stop rather
 * than a loop. Routes are registered by whoever owns them - see
 * RestApi::registerRoutes() - which is what keeps the server ignorant of the
 * API and the API ignorant of how the server was started.
 *
 * The catch-all is registered last and answers 404 to everything the API did not
 * claim, `/` included: the single-page app that will eventually live there does
 * not exist yet, and a device that answered something else would be lying about
 * having a UI.
 */
class HWebServer {
 public:
  HWebServer() = delete;

  /**
   * @brief Starts the server on HWEBSERVER_PORT.
   * @return false if the port could not be bound.
   */
  static bool start() noexcept;

  /** @brief Stops the server. Safe to call when it was never started. */
  static void stop() noexcept;

  /** @brief The running server, or nullptr. Route registration needs it. */
  static httpd_handle_t handle() noexcept;

  /**
   * @brief Registers the 404 catch-all. Call AFTER every real route.
   *
   * Order is not cosmetic: with wildcard matching, the first registered handler
   * whose pattern matches wins, so a slash-wildcard route registered early
   * would swallow the whole API.
   */
  static bool registerFallback() noexcept;
};
