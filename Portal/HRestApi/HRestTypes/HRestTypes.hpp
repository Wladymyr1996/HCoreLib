#pragma once

#include <cstdint>

#include <etl/delegate.h>

class HRestRequest;
class HRestResponse;

/** @brief The methods this API speaks. Everything else is answered 404. */
enum class HRestMethod : uint8_t {
  Get,
  Post,
};

/**
 * @brief Whether a route needs a session key.
 *
 * Checked by HRestApi BEFORE the handler is called, which is the point of
 * having it here rather than leaving each handler to remember: an application
 * route physically cannot forget to guard itself.
 */
enum class HRestAuth : uint8_t {
  None,
  Required,
};

/**
 * @brief What an application registers for a route.
 *
 * An etl::delegate, so it can be a free function or a member of something -
 * and so it costs no allocation.
 *
 * @code
 *   void onValues(HRestRequest& request, HRestResponse& response) {
 *     response.json("200 OK", "{\"temp\":21.4}");
 *   }
 *
 *   HRestApi::add(HRestMethod::Get, "/api/values",
 *                 HRestHandler::create<&onValues>(), HRestAuth::None);
 * @endcode
 *
 * A handler runs in the HTTP server's own task and MUST send exactly one
 * response. One that returns without sending gets an empty 500 from the
 * framework and a warning in the log.
 */
using HRestHandler = etl::delegate<void(HRestRequest&, HRestResponse&)>;
