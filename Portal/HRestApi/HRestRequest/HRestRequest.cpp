#include "HRestApi/HRestRequest/HRestRequest.hpp"

#define HLOG_MODULE_NAME "Api"
#include <HLog/HLog.hpp>

#include <HAuth/HAuth.hpp>

namespace {

/** @brief The header a session key travels in. */
const char* const kAuthHeader = "Authentication-Info";

}  // namespace

HRestRequest::HRestRequest(httpd_req_t* request) noexcept : request_(request) {
}

const char* HRestRequest::uri() const noexcept {
  return (request_ != nullptr) ? request_->uri : "";
}

bool HRestRequest::readBody(char* buffer, size_t size) noexcept {
  if (request_ == nullptr || buffer == nullptr || size == 0) {
    return false;
  }

  const size_t length = static_cast<size_t>(request_->content_len);

  if (length == 0 || length >= size) {
    HWarning("%s: body is %u bytes, which is %s", uri(), static_cast<unsigned>(length),
             (length == 0) ? "empty" : "too long");
    return false;
  }

  size_t received = 0;
  while (received < length) {
    const int chunk = httpd_req_recv(request_, buffer + received, length - received);
    if (chunk <= 0) {
      HWarning("%s: body read failed", uri());
      return false;
    }
    received += static_cast<size_t>(chunk);
  }

  buffer[received] = '\0';
  return true;
}

bool HRestRequest::readJson(HJsonDocument& document, char* buffer, size_t size) noexcept {
  if (!readBody(buffer, size)) {
    return false;
  }

  if (!document.parse(buffer)) {
    HWarning("%s: body is not JSON", uri());
    return false;
  }

  return true;
}

bool HRestRequest::isAuthorised() const noexcept {
  if (request_ == nullptr) {
    return false;
  }

  char key[HAUTH_KEY_BUFFER_SIZE] = "";
  if (httpd_req_get_hdr_value_str(request_, kAuthHeader, key, sizeof(key)) != ESP_OK) {
    return false;
  }

  return HAuth::checkKey(key);
}

httpd_req_t* HRestRequest::raw() const noexcept {
  return request_;
}
