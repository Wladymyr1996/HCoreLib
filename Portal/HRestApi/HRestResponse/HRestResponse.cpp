#include "HRestApi/HRestResponse/HRestResponse.hpp"

#define HLOG_MODULE_NAME "Api"
#include <HLog/HLog.hpp>

HRestResponse::HRestResponse(httpd_req_t* request) noexcept : request_(request), sent_(false) {
}

esp_err_t HRestResponse::json(const char* status, const char* body) noexcept {
  if (request_ == nullptr) {
    return ESP_FAIL;
  }

  if (sent_) {
    // Two sends on one request corrupt the stream, and the second is always the
    // mistake - the first already went out.
    HWarning("%s: a second response was suppressed", request_->uri);
    return ESP_FAIL;
  }

  sent_ = true;

  httpd_resp_set_status(request_, status);
  httpd_resp_set_type(request_, "application/json");

  // Set here so every route answers the same way. A browser tab opened from a
  // file during development is a different origin, and a GET it refuses to read
  // is a GET that looks broken.
  httpd_resp_set_hdr(request_, "Access-Control-Allow-Origin", "*");

  return httpd_resp_send(request_, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t HRestResponse::ok(const char* body) noexcept {
  return json("200 OK", body);
}

esp_err_t HRestResponse::badRequest() noexcept {
  return json("400 Bad Request", "{\"status\":\"bad request\"}");
}

esp_err_t HRestResponse::unauthorized() noexcept {
  return json("401 Unauthorized", "{\"status\":\"unathorized\"}");
}

esp_err_t HRestResponse::notFound() noexcept {
  return json("404 Not Found", "{\"status\":\"not found\"}");
}

bool HRestResponse::sent() const noexcept {
  return sent_;
}
