#pragma once

#include <etl/delegate.h>

#include "HFs/HIFile.hpp"

/**
 * @brief Called once per entry by HIFs::listDir().
 *
 * @param name        The entry's own name, NOT a path. Valid only for the
 *                    duration of the call - copy it if you need it after.
 * @param isDirectory True for a directory, false for a file.
 * @return false to stop the enumeration early, true to continue.
 *
 * A delegate rather than a returned container, because a container would have
 * to be sized for the largest directory anyone might ever list and carried on
 * somebody's stack. This way the backend owns the iteration and the caller
 * owns nothing at all.
 */
using HFsEntryVisitor = etl::delegate<bool(const char* name, bool isDirectory)>;

/**
 * @brief The platform-independent contract for a file system backend.
 *
 * Each platform supplies exactly one implementation (HFsDesktop on Desktop,
 * an HLittleFs on the MCU) and a build compiles in only that one. User code
 * does not touch this interface directly - it calls the HFs::HFileSystem
 * facade (see HFs.hpp), which forwards to the selected backend.
 *
 * Only path-level operations live here. Reading and writing belong to the
 * handle itself (HIFile), so a backend never has to reach into another
 * object's internals to serve them.
 *
 * Note on openFile(): it takes an HIFile& because the interface cannot know
 * the concrete handle type, and an implementation is entitled to cast that
 * reference down to its own handle type in order to attach the descriptor.
 * That cast is sound only because exactly one backend is ever compiled in,
 * which makes the concrete type of every HIFile in the build knowable - see
 * HFsDesktop::openFile() for how this is spelled out at the cast site.
 */
class HIFs {
 public:
  virtual ~HIFs();

  HIFs(const HIFs&) = delete;
  HIFs& operator=(const HIFs&) = delete;

  /**
   * @brief Brings the filesystem up. MUST be called once, before anything else.
   *
   * Every other method fails closed until this has succeeded - on BOTH
   * platforms, which is the point. Desktop has nothing to mount, but a backend
   * that quietly worked without the call would let a device be developed on
   * Windows and then fail on the MCU, where LittleFS genuinely cannot serve a
   * single open() before its partition is mounted. This is the same rule
   * HTask applies to static stacks: the constraint one platform has is
   * enforced on the one that does not, so it fails at your desk rather than
   * during hardware bring-up.
   *
   * Idempotent - calling it twice is not an error.
   *
   * @return false if the filesystem is unusable; the caller should treat that
   *         as fatal for anything configuration-driven.
   */
  virtual bool mount() = 0;

  /** @brief Creates a new empty file, truncating it if it already exists. */
  virtual bool createFile(const char* path) = 0;

  /**
   * @brief Opens `path` into `outFile` using fopen-style `mode`.
   * @param outFile A handle of this backend's own concrete type. Any
   *                previously-open handle in it is closed first.
   */
  virtual bool openFile(const char* path, HIFile& outFile, const char* mode) = 0;

  /** @brief Deletes a file. Fails (returns false) if `path` is a directory. */
  virtual bool deleteFile(const char* path) = 0;

  /**
   * @brief Atomically renames `from` to `to`, REPLACING `to` if it exists.
   *
   * The replacement must be atomic with respect to power loss: an observer
   * (including the next boot) sees either the whole old `to` or the whole new
   * one, never a truncated blend of the two. That is the entire reason this
   * operation exists rather than a delete-then-rename at the call site, and
   * it is what lets HConfig write a `.tmp` and swap it in without a window in
   * which a power cut destroys the live configuration file.
   *
   * Both backends satisfy this: POSIX/`std::filesystem::rename` is atomic
   * within a filesystem, and `lfs_rename` is committed through LittleFS's
   * journal. Neither may be reimplemented as copy-and-delete.
   */
  virtual bool rename(const char* from, const char* to) = 0;

  /** @brief Creates a directory. Returns true if it already exists as a directory. */
  virtual bool createDir(const char* path) = 0;

  /** @brief Removes an empty directory. Fails if it is non-empty or not a directory. */
  virtual bool removeDir(const char* path) = 0;

  /** @brief Returns true if a file or directory exists at `path`. */
  virtual bool exists(const char* path) const = 0;

  /** @brief Returns true if `path` exists and is a directory. */
  virtual bool isDirectory(const char* path) const = 0;

  /**
   * @brief Enumerates the direct children of a directory.
   *
   * Order is whatever the underlying filesystem yields - LittleFS makes no
   * ordering promise and neither does this. `.` and `..` are never reported.
   *
   * An empty path or `"/"` means the ROOT of this filesystem's namespace: the
   * mount point on the MCU, the working directory on Desktop. Every other
   * path is relative to that root, exactly as it is for every other method
   * here - which is what keeps `listDir("config")` meaning the same thing on
   * both platforms.
   *
   * The visitor may recurse into a subdirectory, which costs one open
   * directory handle per level for as long as the nesting lasts.
   *
   * @return false if `path` is not a directory or could not be opened. A
   *         visitor that stopped early still returns true - it did its job.
   */
  virtual bool listDir(const char* path, const HFsEntryVisitor& visitor) const = 0;

 protected:
  HIFs() = default;
};
