#pragma once

#include <cstdio>

#include "HFs/HIFile.hpp"

/**
 * @brief Desktop (Windows/Linux/macOS) HIFile backend: wraps exactly one C
 *        `FILE*` for its whole lifetime.
 *
 * Stack-allocatable, non-copyable, and never allocates from the heap itself.
 * Do not name this class in user code - declare an HFs::HFile (see HFs.hpp),
 * which resolves to this class on Desktop builds. Construct it empty and pass
 * it to HFs::HFileSystem::openFile().
 */
class HFsDesktopFile : public HIFile {
 public:
  HFsDesktopFile();

  /** @brief Closes the file if still open. */
  ~HFsDesktopFile() override;

  /** @brief Reads up to `size` bytes into `buffer`. @return Bytes actually read (0 if not open). */
  size_t read(void* buffer, size_t size) override;

  /** @brief Writes `size` bytes from `buffer`. @return Bytes actually written (0 if not open). */
  size_t write(const void* buffer, size_t size) override;

  /** @brief Moves the read/write position to an absolute byte offset. */
  bool seek(size_t position) override;

  /** @brief Returns the file's total size in bytes (0 if not open). Preserves the current position. */
  size_t size() const override;

  /** @brief Closes the file if open. Safe to call on an already-closed file. */
  void close() override;

  /** @brief Returns true if this object currently holds an open file. */
  bool isOpen() const override;

 private:
  // HFsDesktop::openFile() is what attaches a descriptor to this handle;
  // nothing else may reach the raw FILE*.
  friend class HFsDesktop;

  std::FILE* handle_;
};
