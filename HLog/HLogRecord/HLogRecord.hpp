#pragma once

#include "../HLogLevel/HLogLevel.hpp"

/**
 * @brief One fully resolved log line handed to every enabled backend.
 *
 * HLog does the work that all backends would otherwise repeat - timestamp,
 * thread name and the vsnprintf of the user format - and passes the pieces
 * still separated, so a backend stays free to decide its own layout (colors on
 * the console, CSV in a file, a binary frame over the wire).
 *
 * Every pointer is non-owning and only valid for the duration of the
 * HILogBackend::write() call: a backend that needs to keep the text must copy
 * it into its own fixed-size storage.
 */
struct HLogRecord {
  HLogLevel level;      ///< Severity of the record.
  const char* time;     ///< Formatted timestamp (uptime on MCU, wall clock on desktop).
  const char* module;   ///< HLOG_MODULE_NAME of the emitting translation unit.
  const char* thread;   ///< Task/thread the record was emitted from.
  const char* file;     ///< Source file, or nullptr when not captured.
  int line;             ///< Source line, meaningless when file is nullptr.
  const char* message;  ///< Formatted message, without trailing newline.
};
