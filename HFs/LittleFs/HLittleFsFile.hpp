#pragma once

#include <cstdio>

#include "HFs/HIFile.hpp"

/**
 * @brief ESP32 (LittleFS) HIFile backend: wraps exactly one C `FILE*` for its
 *        whole lifetime.
 *
 * A `FILE*` and not an `lfs_file_t`, because HLittleFs mounts the partition
 * through ESP-IDF's VFS layer. That layer routes stdio at the descriptor
 * table, so `fread` on a path under the mount point lands in LittleFS with no
 * translation of our own - and it costs nothing extra, since the VFS is
 * already linked in for the console. The alternative, calling `lfs_file_read`
 * directly, would mean carrying our own block-device driver over
 * esp_partition and a second buffering scheme beside newlib's.
 *
 * Stack-allocatable, non-copyable, and never allocates from the heap itself.
 * Do not name this class in user code - declare an HFs::HFile (see HFs.hpp),
 * which resolves to this class on MCU builds. Construct it empty and pass it
 * to HFs::HFileSystem::openFile().
 */
class HLittleFsFile : public HIFile {
 public:
  HLittleFsFile();

  /** @brief Closes the file if still open. */
  ~HLittleFsFile() override;

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
  // HLittleFs::openFile() is what attaches a descriptor to this handle;
  // nothing else may reach the raw FILE*.
  friend class HLittleFs;

  std::FILE* handle_;
};
