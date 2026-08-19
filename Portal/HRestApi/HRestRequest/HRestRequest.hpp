#pragma once

#include <cstddef>

#include <esp_http_server.h>

#include <HJson/HJson.hpp>

/** @brief Longest request body accepted. Larger is refused, never truncated. */
#ifndef HRESTREQUEST_MAX_BODY
#define HRESTREQUEST_MAX_BODY 256
#endif

/**
 * @brief One incoming request, in terms a handler can use.
 *
 * A thin wrapper over the server's own request, so an application handler deals
 * in bodies and JSON rather than in socket reads and header lookups. It owns no
 * memory: the caller supplies the buffers, which is what keeps this allocation
 * free and lets a handler decide how much stack it is willing to spend.
 */
class HRestRequest {
 public:
  explicit HRestRequest(httpd_req_t* request) noexcept;

  /** @brief The path that matched, for logging. Never null. */
  const char* uri() const noexcept;

  /**
   * @brief Reads the whole body into `buffer`.
   * @return false when it is empty, longer than the buffer, or the socket
   *         failed - each logged, so a handler can simply answer 400.
   */
  bool readBody(char* buffer, size_t size) noexcept;

  /**
   * @brief Reads the body and parses it as JSON.
   * @param document A document backed by the caller's own pool.
   * @param buffer Scratch for the raw body; must outlive the parsed document,
   *        because strings in it are copied into the pool but the parse reads
   *        from here.
   * @return false if the body could not be read or is not JSON.
   */
  bool readJson(HJsonDocument& document, char* buffer, size_t size) noexcept;

  /**
   * @brief True when the request carries a valid session key.
   *
   * Handlers rarely need this: HRestApi has already enforced HRestAuth::Required
   * before calling one. It is here for a route that is open but wants to answer
   * differently for an administrator.
   */
  bool isAuthorised() const noexcept;

  /** @brief The underlying request, for anything this wrapper does not cover. */
  httpd_req_t* raw() const noexcept;

 private:
  httpd_req_t* request_;
};
