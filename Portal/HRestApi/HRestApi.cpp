#include "HRestApi/HRestApi.hpp"

#define HLOG_MODULE_NAME "Api"
#include <HLog/HLog.hpp>

#include <cstdio>
#include <cstring>

#include <esp_app_desc.h>

#include <etl/vector.h>

#include <HAuth/HAuth.hpp>
#include "HFactoryReset/HFactoryReset.hpp"

namespace {

/** @brief Pool a request's JSON is carved from. Sized for the bodies above. */
constexpr size_t kJsonPoolSize = 512;

/** @brief Longest response the library builds. */
constexpr size_t kResponseBufferSize = 192;

/** @brief Longest password accepted, terminator included. */
constexpr size_t kPasswordBufferSize = 64;

httpd_handle_t server = nullptr;

/**
 * @brief One registered application route.
 *
 * The handler and its auth requirement, kept beside the httpd registration so
 * the trampoline can find both from `user_ctx`.
 */
struct Route {
  HRestHandler handler;
  HRestAuth auth;
};

using Routes = etl::vector<Route, HRESTAPI_MAX_ROUTES>;

/**
 * @brief The application's routes.
 *
 * etl::vector never reallocates, so a pointer handed to httpd as user_ctx stays
 * valid for the life of the server - which is exactly why a fixed-capacity
 * container is the right one here rather than merely the cheap one.
 */
Routes& routes() noexcept {
  static Routes table;
  return table;
}

/**
 * @brief Copies a JSON string field into a null-terminated buffer.
 *
 * asString() hands back a view into the document's pool, and every caller here
 * needs a C string; doing it once keeps the truncation rule in one place.
 */
void copyField(const HJsonValue& root, const char* key, char* out, size_t size) {
  const std::string_view text = root[key].asString();
  const size_t length = (text.size() < size - 1) ? text.size() : size - 1;

  std::memcpy(out, text.data(), length);
  out[length] = '\0';
}

// ---------------------------------------------------------------------------
// The library's own routes
// ---------------------------------------------------------------------------

esp_err_t handleAuthStatus(httpd_req_t* raw) {
  HRestRequest request(raw);
  HRestResponse response(raw);

  const char* status = "unathorized";

  if (!HAuth::isPasswordSet()) {
    // "first" outranks a valid key on purpose: a device with no password has
    // nothing to authorise against, and what a UI should offer is the chance to
    // set one.
    status = "first";
  } else if (request.isAuthorised()) {
    status = "ok";
  }

  char body[kResponseBufferSize] = "";
  std::snprintf(body, sizeof(body), "{\"status\":\"%s\"}", status);
  return response.json("200 OK", body);
}

esp_err_t handleAuth(httpd_req_t* raw) {
  HRestRequest request(raw);
  HRestResponse response(raw);

  char body[HRESTREQUEST_MAX_BODY] = "";
  static char pool[kJsonPoolSize];
  HJsonDocument document(pool, sizeof(pool));

  if (!request.readJson(document, body, sizeof(body))) {
    return response.json("401 Unauthorized", "{\"auth_key\":null}");
  }

  char password[kPasswordBufferSize] = "";
  copyField(document.getRoot(), "pass", password, sizeof(password));

  if (!HAuth::verify(password)) {
    HWarning("/api/auth: wrong password");
    return response.json("401 Unauthorized", "{\"auth_key\":null}");
  }

  char key[HAUTH_KEY_BUFFER_SIZE] = "";
  if (!HAuth::issueKey(key, sizeof(key))) {
    return response.json("500 Internal Server Error", "{\"auth_key\":null}");
  }

  char answer[kResponseBufferSize] = "";
  std::snprintf(answer, sizeof(answer), "{\"auth_key\":\"%s\"}", key);
  return response.json("200 OK", answer);
}

esp_err_t handleSetAdminPassword(httpd_req_t* raw) {
  HRestRequest request(raw);
  HRestResponse response(raw);

  // The one asymmetry in this API: while no password exists, setting one needs
  // no key. A device that shipped locked would be a device nobody can configure.
  if (HAuth::isPasswordSet() && !request.isAuthorised()) {
    return response.unauthorized();
  }

  char body[HRESTREQUEST_MAX_BODY] = "";
  static char pool[kJsonPoolSize];
  HJsonDocument document(pool, sizeof(pool));

  if (!request.readJson(document, body, sizeof(body))) {
    return response.badRequest();
  }

  char password[kPasswordBufferSize] = "";
  copyField(document.getRoot(), "pass", password, sizeof(password));

  if (!HAuth::setPassword(password)) {
    return response.badRequest();
  }

  return response.ok();
}

esp_err_t handleFactoryReset(httpd_req_t* raw) {
  HRestRequest request(raw);
  HRestResponse response(raw);

  // A key ALWAYS, even on a device with no admin password. Every other route is
  // either harmless or recoverable; this one throws away everything the owner
  // ever told the device.
  if (!request.isAuthorised()) {
    return response.unauthorized();
  }

  const esp_err_t sent = response.ok();

  // AFTER the answer is on the wire. Rebooting first would drop the socket and
  // the client would report a failure for something about to succeed.
  HFactoryReset::request();

  return sent;
}

esp_err_t handleInfo(httpd_req_t* raw) {
  HRestResponse response(raw);

  const esp_app_desc_t* description = esp_app_get_description();

  char body[kResponseBufferSize] = "";
  std::snprintf(body, sizeof(body), "{\"name\":\"%s\",\"fw\":\"%s\",\"idf\":\"%s\"}",
                (description != nullptr) ? description->project_name : "unknown",
                (description != nullptr) ? description->version : "unknown",
                (description != nullptr) ? description->idf_ver : "unknown");

  return response.json("200 OK", body);
}

esp_err_t handleOptions(httpd_req_t* raw) {
  // A POST carrying `Content-Type: application/json` and an
  // `Authentication-Info` header is not a "simple" request, so a browser sends
  // OPTIONS first and refuses to send the real one unless this answers.
  httpd_resp_set_status(raw, "204 No Content");
  httpd_resp_set_hdr(raw, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(raw, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  httpd_resp_set_hdr(raw, "Access-Control-Allow-Headers", "Content-Type, Authentication-Info");
  httpd_resp_set_hdr(raw, "Access-Control-Max-Age", "600");

  return httpd_resp_send(raw, nullptr, 0);
}

esp_err_t handleApiNotFound(httpd_req_t* raw) {
  HRestResponse response(raw);
  HWarning("no route for %s", raw->uri);
  return response.notFound();
}

/**
 * @brief The single entry point for every application route.
 *
 * httpd calls C functions, and an etl::delegate is not one - so every registered
 * route points here, and `user_ctx` says which of them it was. One trampoline,
 * however many routes.
 */
esp_err_t handleApplicationRoute(httpd_req_t* raw) {
  const Route* route = static_cast<const Route*>(raw->user_ctx);

  HRestRequest request(raw);
  HRestResponse response(raw);

  if (route == nullptr || !route->handler.is_valid()) {
    return response.json("500 Internal Server Error", "{\"status\":\"no handler\"}");
  }

  // Enforced BEFORE the handler, which is why routes declare what they need
  // rather than checking it themselves.
  if (route->auth == HRestAuth::Required && !request.isAuthorised()) {
    return response.unauthorized();
  }

  route->handler(request, response);

  if (!response.sent()) {
    HWarning("%s: the handler sent nothing", raw->uri);
    return response.json("500 Internal Server Error", "{\"status\":\"no answer\"}");
  }

  return ESP_OK;
}

const httpd_uri_t kLibraryRoutes[] = {
    {.uri = "/api/auth", .method = HTTP_GET, .handler = &handleAuthStatus, .user_ctx = nullptr},
    {.uri = "/api/auth", .method = HTTP_POST, .handler = &handleAuth, .user_ctx = nullptr},
    {.uri = "/api/setAdminPassword",
     .method = HTTP_POST,
     .handler = &handleSetAdminPassword,
     .user_ctx = nullptr},
    {.uri = "/api/factoryReset",
     .method = HTTP_POST,
     .handler = &handleFactoryReset,
     .user_ctx = nullptr},
    {.uri = "/api/info", .method = HTTP_GET, .handler = &handleInfo, .user_ctx = nullptr},
};

const httpd_uri_t kCatchAllRoutes[] = {
    {.uri = "/*", .method = HTTP_OPTIONS, .handler = &handleOptions, .user_ctx = nullptr},
    {.uri = "/api/*", .method = HTTP_GET, .handler = &handleApiNotFound, .user_ctx = nullptr},
    {.uri = "/api/*", .method = HTTP_POST, .handler = &handleApiNotFound, .user_ctx = nullptr},
};

}  // namespace

bool HRestApi::begin(httpd_handle_t handle) noexcept {
  if (handle == nullptr) {
    return false;
  }

  server = handle;
  routes().clear();

  for (const httpd_uri_t& route : kLibraryRoutes) {
    if (httpd_register_uri_handler(server, &route) != ESP_OK) {
      HCritical("could not register %s", route.uri);
      return false;
    }
  }

  HInfo("%u library route(s) registered",
        static_cast<unsigned>(sizeof(kLibraryRoutes) / sizeof(kLibraryRoutes[0])));
  return true;
}

bool HRestApi::add(HRestMethod method, const char* uri, const HRestHandler& handler,
                   HRestAuth auth) noexcept {
  if (server == nullptr || uri == nullptr || !handler.is_valid()) {
    return false;
  }

  Routes& table = routes();
  if (table.full()) {
    HCritical("no room for %s - raise HRESTAPI_MAX_ROUTES", uri);
    return false;
  }

  table.push_back(Route{handler, auth});

  httpd_uri_t registration = {};
  registration.uri = uri;
  registration.method = (method == HRestMethod::Post) ? HTTP_POST : HTTP_GET;
  registration.handler = &handleApplicationRoute;
  registration.user_ctx = &table.back();

  if (httpd_register_uri_handler(server, &registration) != ESP_OK) {
    HCritical("could not register %s", uri);
    table.pop_back();
    return false;
  }

  HDebug("%s registered (%s)", uri, (auth == HRestAuth::Required) ? "key required" : "open");
  return true;
}

bool HRestApi::finish() noexcept {
  if (server == nullptr) {
    return false;
  }

  for (const httpd_uri_t& route : kCatchAllRoutes) {
    if (httpd_register_uri_handler(server, &route) != ESP_OK) {
      HCritical("could not register the catch-all %s", route.uri);
      return false;
    }
  }

  return true;
}
