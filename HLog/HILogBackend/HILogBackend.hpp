#pragma once

#include "../HLogRecord/HLogRecord.hpp"

/**
 * @brief Sink a log record can be written to.
 *
 * Implement this on the application side to send logs somewhere HCoreLib knows
 * nothing about - a file, a ring buffer, a network socket, a display - and
 * register the instance with HLog::addBackend(). HLog only stores a pointer, so
 * a backend must outlive the logging system: give it static storage duration.
 *
 * write() is always called with HLog's lock held, so implementations do not
 * need their own locking, but they must not call back into HLog (that would
 * deadlock) and should return quickly - every log call pays for every backend.
 */
class HILogBackend {
 public:
  virtual ~HILogBackend();

  /**
   * @brief Emits one record. Called only while this backend is enabled.
   * @param record Fully formatted record, valid only for this call.
   */
  virtual void write(const HLogRecord& record) noexcept = 0;
};
