#pragma once

#include "../HILogBackend/HILogBackend.hpp"

/**
 * @brief Default backend: writes ANSI-colored lines to stdout.
 *
 * On the target this is the IDF monitor / UART console, on desktop the
 * terminal. HLog registers one instance of this class as backend 0 by itself -
 * an application only needs this header if it wants a second console sink or is
 * checking the type of what sits at HLog::CONSOLE_BACKEND_INDEX.
 *
 * Colors are ANSI escapes; a console that does not understand them shows them
 * as noise, in which case disable this backend and register a plain one.
 */
class HConsoleLogBackend : public HILogBackend {
 public:
  /**
   * @brief Prints one record as "[time][LEVEL][module][thread](file:line): message".
   * @param record Record to print.
   */
  void write(const HLogRecord& record) noexcept override;
};
