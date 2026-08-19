#pragma once

#include <esp_http_server.h>

/**
 * @brief The one answer a handler gives, and the headers that go with it.
 *
 * Every reply from this API is JSON, so the content type, the CORS header and
 * the status line are set in one place rather than in every handler - and a
 * route that answers differently from the rest is then a deliberate act rather
 * than an oversight.
 *
 * Exactly one send per request. A second is refused and logged rather than
 * corrupting the stream.
 */
class HRestResponse {
 public:
  explicit HRestResponse(httpd_req_t* request) noexcept;

  /**
   * @brief Sends `body` with an explicit status line.
   * @param status Full HTTP status, e.g. "200 OK" or "404 Not Found".
   * @param body JSON text. Not copied - it only has to outlive this call.
   */
  esp_err_t json(const char* status, const char* body) noexcept;

  /** @brief 200 with `{"status":"ok"}`, or a body of your own. */
  esp_err_t ok(const char* body = "{\"status\":\"ok\"}") noexcept;

  /** @brief 400 with `{"status":"bad request"}`. */
  esp_err_t badRequest() noexcept;

  /**
   * @brief 401 with `{"status":"unathorized"}`.
   *
   * The spelling is deliberate and matches what clients already match on.
   */
  esp_err_t unauthorized() noexcept;

  /** @brief 404 with `{"status":"not found"}`. */
  esp_err_t notFound() noexcept;

  /** @brief True once something has been sent. */
  bool sent() const noexcept;

 private:
  httpd_req_t* request_;
  bool sent_;
};
