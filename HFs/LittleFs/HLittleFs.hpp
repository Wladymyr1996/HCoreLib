#pragma once

// For <HCoreLibConfig.h>: every knob below is overridable by the application.
#include <HCoreLib.h>

#include <cstddef>

#include "HFs/HIFs.hpp"
#include "HFs/LittleFs/HLittleFsFile.hpp"

/** Flash partition the filesystem lives in. Must exist in partitions.csv as `data, littlefs`. */
#ifndef HLITTLEFS_PARTITION_LABEL
#define HLITTLEFS_PARTITION_LABEL "storage"
#endif

/** VFS mount point. Every path handed to this backend is resolved beneath it. */
#ifndef HLITTLEFS_BASE_PATH
#define HLITTLEFS_BASE_PATH "/littlefs"
#endif

/**
 * Format the partition when it cannot be mounted.
 *
 * On by default because the alternative on a factory-fresh board is a device
 * that boots into a permanently unusable filesystem: a virgin partition is
 * unformatted by definition, and there is no other moment at which anyone
 * would format it. Set to 0 where losing an existing filesystem to a
 * transient mount failure is worse than never mounting at all.
 */
#ifndef HLITTLEFS_FORMAT_IF_MOUNT_FAILED
#define HLITTLEFS_FORMAT_IF_MOUNT_FAILED 1
#endif

/** Buffer for a mount-point-prefixed path ("/littlefs" + "/" + the caller's path). */
#ifndef HLITTLEFS_MAX_PATH_LEN
#define HLITTLEFS_MAX_PATH_LEN 128
#endif

/**
 * @brief ESP32 HIFs backend, built on LittleFS through ESP-IDF's VFS.
 *
 * Do not name this class in user code - call HFs::HFileSystem (see HFs.hpp),
 * which forwards to the single instance of whichever backend this build
 * selected. One static instance lives in HFs.cpp; nothing here is ever
 * heap-allocated.
 *
 * **Paths are relative and get prefixed.** Callers pass the same plain paths
 * the Desktop backend takes - `createFile("config/Main.cfg")` - and this class
 * resolves them under HLITTLEFS_BASE_PATH before touching the VFS. That
 * mapping is the reason the mount point never appears in HConfig or in any
 * unit: application code stays free of a path convention that exists only on
 * the MCU. Relying on the process working directory instead would put the same
 * knowledge in a global nobody owns.
 *
 * **mount() is mandatory and not automatic.** It is a separate call rather
 * than constructor work because it can fail, and a constructor running during
 * static initialization has nowhere to report that to - and because formatting
 * a filesystem is not something that should happen as a side effect of a
 * global being created. Every other method fails closed until it succeeds.
 */
class HLittleFs : public HIFs {
 public:
  HLittleFs() = default;
  ~HLittleFs() override;

  /**
   * @brief Mounts HLITTLEFS_PARTITION_LABEL at HLITTLEFS_BASE_PATH.
   *
   * Idempotent: mounting an already-mounted backend succeeds without touching
   * flash. Formats the partition first when it cannot be mounted and
   * HLITTLEFS_FORMAT_IF_MOUNT_FAILED is set - which is what makes a
   * factory-fresh board usable on its first boot.
   */
  bool mount() override;

  /** @brief Creates a new empty file, truncating it if it already exists. */
  bool createFile(const char* path) override;

  /**
   * @brief Opens `path` into `outFile` using fopen-style `mode`. Any
   *        previously-open handle in `outFile` is closed first.
   * @param outFile Must be an HLittleFsFile - see the note in the
   *                implementation on why that is guaranteed.
   */
  bool openFile(const char* path, HIFile& outFile, const char* mode) override;

  /** @brief Deletes a file. Fails (returns false) if `path` is a directory. */
  bool deleteFile(const char* path) override;

  /** @brief Atomically renames `from` to `to`, replacing `to` if it exists. */
  bool rename(const char* from, const char* to) override;

  /** @brief Creates a directory. Returns true if it already exists as a directory. */
  bool createDir(const char* path) override;

  /** @brief Removes an empty directory. Fails if it is non-empty or not a directory. */
  bool removeDir(const char* path) override;

  /** @brief Returns true if a file or directory exists at `path`. */
  bool exists(const char* path) const override;

  /** @brief Returns true if `path` exists and is a directory. */
  bool isDirectory(const char* path) const override;

  /** @brief Enumerates the direct children of a directory. See HIFs::listDir(). */
  bool listDir(const char* path, const HFsEntryVisitor& visitor) const override;

 private:
  /**
   * @brief Resolves a caller's path to an absolute VFS path under the mount point.
   *
   * Strips leading slashes off `path` so that both "config/x.cfg" and
   * "/config/x.cfg" produce one canonical result - LittleFS is under no
   * obligation to collapse a doubled separator the way Windows and POSIX do.
   *
   * @return false if the path is missing, empty, or too long for `outBuffer`.
   */
  static bool buildPath(const char* path, char* outBuffer, size_t outSize);

  // Initialized here rather than in a constructor body so the single backend
  // object in HFs.cpp stays constant-initialized - see the note there.
  bool mounted_ = false;
};
