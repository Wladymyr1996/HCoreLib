#pragma once

#include "HFs/Desktop/HFsDesktopFile.hpp"
#include "HFs/HIFs.hpp"

/**
 * @brief Desktop (Windows/Linux/macOS) HIFs backend, built on `std::fopen`
 *        and `<filesystem>`.
 *
 * Do not name this class in user code - call HFs::HFileSystem (see HFs.hpp),
 * which forwards to the single instance of whichever backend this build
 * selected. The object holds no state of its own: one static instance lives
 * in HFs.cpp, so nothing here is ever heap-allocated.
 *
 * Paths are plain, relative-to-the-current-working-directory paths handed
 * straight to fopen()/std::filesystem, e.g. `createFile("config.json")`.
 */
class HFsDesktop : public HIFs {
 public:
  HFsDesktop() = default;
  ~HFsDesktop() override;

  /**
   * @brief Marks the filesystem usable. Always succeeds - there is nothing to mount.
   *
   * It still has to be CALLED: every other method returns false beforehand.
   * The host has no mount step of its own, so this flag exists purely to make
   * the MCU's requirement fail here too - see HIFs::mount().
   */
  bool mount() override;

  /** @brief Creates a new empty file, truncating it if it already exists. */
  bool createFile(const char* path) override;

  /**
   * @brief Opens `path` into `outFile` using fopen-style `mode`. Any
   *        previously-open handle in `outFile` is closed first.
   * @param outFile Must be an HFsDesktopFile - see the note in the
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
  // Initialized here rather than in a constructor body so the single backend
  // object in HFs.cpp stays constant-initialized - see the note there.
  bool mounted_ = false;
};
