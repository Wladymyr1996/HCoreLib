#pragma once

#include <HCoreLib.h>

#include "HFs/HIFile.hpp"
#include "HFs/HIFs.hpp"

#if IS_MCU

#include "HFs/LittleFs/HLittleFs.hpp"
#include "HFs/LittleFs/HLittleFsFile.hpp"

#else

#include "HFs/Desktop/HFsDesktop.hpp"
#include "HFs/Desktop/HFsDesktopFile.hpp"

#endif

/**
 * @brief Compile-time-selected file system, the only names user code should use.
 *
 *   HFs::HFileSystem::mount();          // once, at startup, before anything else
 *
 *   HFs::HFile file;
 *   if (HFs::HFileSystem::openFile("config/Main.cfg", file, "r")) {
 *     char buffer[64];
 *     const size_t bytesRead = file.read(buffer, sizeof(buffer));
 *     file.close();
 *   }
 *
 * How the platform split works here: HIFs/HIFile define the contract, each
 * platform backend implements it, and exactly one backend is compiled into a
 * given build - selected above by IS_MCU (see HCoreLib.h). There is no runtime
 * backend switching and no dead platform code linked in.
 *
 * HFileSystem is a thin static facade over that single backend instance, not
 * a class you instantiate. It exists so call sites stay short and stable
 * (`HFs::HFileSystem::exists(p)`) no matter which backend is underneath, and
 * so the backend instance itself has exactly one owner - a static object in
 * HFs.cpp, never a heap allocation.
 */
namespace HFs {

/** @brief The single compiled-in backend instance the facade forwards to. */
HIFs& instance();

/** @brief HIFile handle type for the selected backend. Declare these on the stack. */
#if IS_MCU
using HFile = HLittleFsFile;
#else
using HFile = HFsDesktopFile;
#endif

/** @brief Static facade forwarding every call to instance(). Never instantiated. */
class HFileSystem {
 public:
  HFileSystem() = delete;

  /**
   * @brief Brings the filesystem up. Call once at startup, before anything
   *        else here and before HConfig::init(). See HIFs::mount().
   */
  static bool mount();

  /** @brief Creates a new empty file, truncating it if it already exists. */
  static bool createFile(const char* path);

  /** @brief Opens `path` into `outFile` using fopen-style `mode`. Any previously-open handle in `outFile` is closed first. */
  static bool openFile(const char* path, HIFile& outFile, const char* mode);

  /** @brief Deletes a file. Fails (returns false) if `path` is a directory. */
  static bool deleteFile(const char* path);

  /** @brief Atomically renames `from` to `to`, replacing `to` if it exists. See HIFs::rename. */
  static bool rename(const char* from, const char* to);

  /** @brief Creates a directory. Returns true if it already exists as a directory. */
  static bool createDir(const char* path);

  /** @brief Removes an empty directory. Fails if it is non-empty or not a directory. */
  static bool removeDir(const char* path);

  /** @brief Returns true if a file or directory exists at `path`. */
  static bool exists(const char* path);

  /** @brief Returns true if `path` exists and is a directory. */
  static bool isDirectory(const char* path);

  /**
   * @brief Enumerates the direct children of a directory. See HIFs::listDir().
   *
   * @code
   *   bool onEntry(const char* name, bool isDirectory) { ...; return true; }
   *   HFs::HFileSystem::listDir("config", HFsEntryVisitor::create<&onEntry>());
   * @endcode
   */
  static bool listDir(const char* path, const HFsEntryVisitor& visitor);
};

}  // namespace HFs
