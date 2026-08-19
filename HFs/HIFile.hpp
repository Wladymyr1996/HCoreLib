#pragma once

#include <cstddef>

/**
 * @brief The platform-independent contract for a single open file handle.
 *
 * Each platform backend supplies its own implementation (HFsDesktopFile on
 * Desktop, an HLittleFsFile on the MCU). User code never names one directly:
 * it declares an HFs::HFile (see HFs.hpp), which is an alias for whichever
 * implementation this build selected.
 *
 * Handles are always constructed empty and stack-allocatable - no
 * implementation may allocate from the heap. Open one by passing it to
 * HFs::HFileSystem::openFile(); the handle itself only ever reads, writes,
 * seeks and closes what it was handed.
 *
 *   HFs::HFile file;
 *   if (HFs::HFileSystem::openFile("config/Main.cfg", file, "r")) {
 *     char buffer[64];
 *     const size_t bytesRead = file.read(buffer, sizeof(buffer));
 *     file.close();
 *   }
 *
 * Non-copyable by design: a handle owns exactly one underlying descriptor for
 * its whole lifetime, and copying it would mean two objects closing the same
 * descriptor.
 */
class HIFile {
 public:
  /** @brief Closes the file if still open. */
  virtual ~HIFile();

  HIFile(const HIFile&) = delete;
  HIFile& operator=(const HIFile&) = delete;

  /** @brief Reads up to `size` bytes into `buffer`. @return Bytes actually read (0 if not open). */
  virtual size_t read(void* buffer, size_t size) = 0;

  /** @brief Writes `size` bytes from `buffer`. @return Bytes actually written (0 if not open). */
  virtual size_t write(const void* buffer, size_t size) = 0;

  /** @brief Moves the read/write position to an absolute byte offset. */
  virtual bool seek(size_t position) = 0;

  /** @brief Returns the file's total size in bytes (0 if not open). Preserves the current position. */
  virtual size_t size() const = 0;

  /** @brief Closes the file if open. Safe to call on an already-closed file. */
  virtual void close() = 0;

  /** @brief Returns true if this object currently holds an open file. */
  virtual bool isOpen() const = 0;

 protected:
  HIFile() = default;
};
