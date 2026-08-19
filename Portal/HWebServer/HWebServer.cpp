#include "HWebServer/HWebServer.hpp"

#define HLOG_MODULE_NAME "Web"
#include <HLog/HLog.hpp>

#include <HCoreLib.h>

namespace {

httpd_handle_t server = nullptr;

/** @brief Answers everything the API did not claim. */
esp_err_t notFound(httpd_req_t* request) {
  HInfo("404 %s", request->uri);

  httpd_resp_set_status(request, "404 Not Found");
  httpd_resp_set_type(request, "application/json");

  // JSON rather than a page, because every client this device has right now
  // speaks JSON - and the single-page app that will one day live at / can
  // replace this handler without touching anything else.
  const char* body = "{\"status\":\"not found\"}";
  return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}

/*
 * POST only. Every unclaimed GET is taken by StaticUi, which redirects it to the
 * page - that is what turns a phone's connectivity probe into a portal prompt.
 * Registering a GET here as well would be a route nothing can ever reach.
 */
const httpd_uri_t kFallbackPost = {
    .uri = "/*", .method = HTTP_POST, .handler = &notFound, .user_ctx = nullptr};

}  // namespace

bool HWebServer::start() noexcept {
  if (server != nullptr) {
    return true;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = HWEBSERVER_PORT;

  // Wildcard matching, for the catch-all. Without it "/*" would be a literal
  // path nobody ever requests.
  config.uri_match_fn = httpd_uri_match_wildcard;

  // Six: five API routes plus the two fallbacks, rounded up. The default is
  // eight, but naming it here means adding a route fails loudly at start-up
  // rather than silently at the seventh registration.
  config.max_uri_handlers = HWEBSERVER_MAX_ROUTES;

  const esp_err_t result = httpd_start(&server, &config);
  if (result != ESP_OK) {
    HCritical("could not start on port %d: %s", HWEBSERVER_PORT, esp_err_to_name(result));
    server = nullptr;
    return false;
  }

  HInfo("listening on port %d", HWEBSERVER_PORT);
  return true;
}

void HWebServer::stop() noexcept {
  if (server == nullptr) {
    return;
  }

  httpd_stop(server);
  server = nullptr;
  HInfo("stopped");
}

httpd_handle_t HWebServer::handle() noexcept {
  return server;
}

bool HWebServer::registerFallback() noexcept {
  if (server == nullptr) {
    return false;
  }

  return httpd_register_uri_handler(server, &kFallbackPost) == ESP_OK;
}
