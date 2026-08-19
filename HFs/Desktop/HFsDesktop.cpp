#include "HFs/Desktop/HFsDesktop.hpp"

#include <filesystem>

HFsDesktop::~HFsDesktop() {
}

bool HFsDesktop::mount() {
  // Nothing to do: the host filesystem is already there. The flag exists so
  // that skipping this call fails on Windows exactly as it would on the MCU,
  // where LittleFS cannot serve anything before its partition is mounted.
  mounted_ = true;
  return true;
}

bool HFsDesktop::createFile(const char* path) {
  if (!mounted_ || path == nullptr) {
    return false;
  }

  std::FILE* handle = std::fopen(path, "w");
  if (handle == nullptr) {
    return false;
  }
  std::fclose(handle);
  return true;
}

bool HFsDesktop::openFile(const char* path, HIFile& outFile, const char* mode) {
  // HIFs::openFile has to speak in terms of the interface, but attaching a
  // FILE* means reaching into this backend's own handle type. The cast is
  // sound because HFs.hpp compiles in exactly one backend per build: on a
  // Desktop build, HFs::HFile IS HFsDesktopFile, so no other kind of HIFile
  // can reach this function. Should a build ever link two backends at once,
  // this is the line that breaks - and the static_assert-free alternative
  // (a virtual attach() on HIFile) would only move the same assumption
  // elsewhere.
  HFsDesktopFile& file = static_cast<HFsDesktopFile&>(outFile);

  file.close();  // Defensive: never leak a previously-open handle in outFile.

  if (!mounted_ || path == nullptr || mode == nullptr) {
    return false;
  }

  std::FILE* handle = std::fopen(path, mode);
  if (handle == nullptr) {
    return false;
  }

  file.handle_ = handle;
  return true;
}

bool HFsDesktop::exists(const char* path) const {
  if (!mounted_ || path == nullptr) {
    return false;
  }
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

bool HFsDesktop::isDirectory(const char* path) const {
  if (!mounted_ || path == nullptr) {
    return false;
  }
  std::error_code ec;
  return std::filesystem::is_directory(path, ec);
}

bool HFsDesktop::listDir(const char* path, const HFsEntryVisitor& visitor) const {
  if (!mounted_ || path == nullptr || !visitor.is_valid()) {
    return false;
  }

  // The root of this filesystem's namespace is the working directory here, the
  // mount point on the MCU. Spelling it "" or "/" means the same thing on both.
  if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
    path = ".";
  }

  if (!isDirectory(path)) {
    return false;
  }

  std::error_code ec;
  // The non-throwing overload, and then ec is checked on every step: this
  // build has no exceptions, so the throwing iterator would call std::terminate
  // on a directory that vanishes mid-walk.
  std::filesystem::directory_iterator it(path, ec);
  if (ec) {
    return false;
  }

  const std::filesystem::directory_iterator end;
  while (it != end) {
    const bool isDir = it->is_directory(ec) && !ec;
    if (!visitor(it->path().filename().string().c_str(), isDir)) {
      return true;  // Stopped early on purpose, which is not a failure.
    }

    it.increment(ec);
    if (ec) {
      return false;
    }
  }
  return true;
}

bool HFsDesktop::deleteFile(const char* path) {
  if (!mounted_ || path == nullptr || isDirectory(path)) {
    return false;  // Directories must go through removeDir().
  }
  std::error_code ec;
  return std::filesystem::remove(path, ec);
}

bool HFsDesktop::rename(const char* from, const char* to) {
  if (!mounted_ || from == nullptr || to == nullptr) {
    return false;
  }

  // std::filesystem::rename, NOT std::rename: the C function fails when the
  // destination exists on Windows, which is precisely the case HConfig's
  // swap-in-the-new-file needs. The <filesystem> version is specified to
  // replace an existing file and maps to MoveFileEx(MOVEFILE_REPLACE_EXISTING)
  // there, matching POSIX and lfs_rename.
  std::error_code ec;
  std::filesystem::rename(from, to, ec);
  return !ec;
}

bool HFsDesktop::createDir(const char* path) {
  if (!mounted_ || path == nullptr) {
    return false;
  }
  if (exists(path)) {
    // Idempotent: an existing directory is success; a file at that path is a real conflict.
    return isDirectory(path);
  }
  std::error_code ec;
  return std::filesystem::create_directory(path, ec);
}

bool HFsDesktop::removeDir(const char* path) {
  if (!mounted_ || path == nullptr || !isDirectory(path)) {
    return false;
  }
  // std::filesystem::remove only removes a single empty entry (unlike
  // remove_all), matching the POSIX rmdir() semantics an MCU LittleFS
  // backend will have: a non-empty directory fails rather than being
  // silently wiped.
  std::error_code ec;
  return std::filesystem::remove(path, ec);
}
