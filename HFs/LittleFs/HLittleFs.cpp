#define HLOG_MODULE_NAME "HLittleFs"

#include "HFs/LittleFs/HLittleFs.hpp"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <esp_littlefs.h>

#include "HLog/HLog.hpp"

HLittleFs::~HLittleFs() {
}

bool HLittleFs::mount() {
  if (mounted_) {
    return true;  // Idempotent: re-mounting must not touch flash.
  }

  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = HLITTLEFS_BASE_PATH;
  conf.partition_label = HLITTLEFS_PARTITION_LABEL;
  conf.format_if_mount_failed = HLITTLEFS_FORMAT_IF_MOUNT_FAILED;

  const esp_err_t result = esp_vfs_littlefs_register(&conf);
  if (result != ESP_OK) {
    // Worth naming the partition: by far the most common cause is a
    // partitions.csv without a `data, littlefs` entry under this label, and
    // the raw esp_err_t alone sends people looking at the filesystem instead.
    HCritical("mount of partition '%s' failed: %s", HLITTLEFS_PARTITION_LABEL, esp_err_to_name(result));
    return false;
  }

  size_t totalBytes = 0;
  size_t usedBytes = 0;
  if (esp_littlefs_info(HLITTLEFS_PARTITION_LABEL, &totalBytes, &usedBytes) == ESP_OK) {
    HInfo("mounted '%s' at %s: %u of %u bytes used",
          HLITTLEFS_PARTITION_LABEL, HLITTLEFS_BASE_PATH,
          static_cast<unsigned>(usedBytes), static_cast<unsigned>(totalBytes));
  } else {
    HInfo("mounted '%s' at %s", HLITTLEFS_PARTITION_LABEL, HLITTLEFS_BASE_PATH);
  }

  mounted_ = true;
  return true;
}

bool HLittleFs::buildPath(const char* path, char* outBuffer, size_t outSize) {
  if (path == nullptr || outBuffer == nullptr || outSize == 0) {
    return false;
  }
  while (*path == '/') {
    ++path;
  }
  if (*path == '\0') {
    return false;
  }

  const int written = snprintf(outBuffer, outSize, "%s/%s", HLITTLEFS_BASE_PATH, path);
  return written > 0 && static_cast<size_t>(written) < outSize;
}

bool HLittleFs::createFile(const char* path) {
  char resolved[HLITTLEFS_MAX_PATH_LEN];
  if (!mounted_ || !buildPath(path, resolved, sizeof(resolved))) {
    return false;
  }

  std::FILE* handle = std::fopen(resolved, "w");
  if (handle == nullptr) {
    return false;
  }
  std::fclose(handle);
  return true;
}

bool HLittleFs::openFile(const char* path, HIFile& outFile, const char* mode) {
  // HIFs::openFile has to speak in terms of the interface, but attaching a
  // FILE* means reaching into this backend's own handle type. The cast is
  // sound because HFs.hpp compiles in exactly one backend per build: on an MCU
  // build, HFs::HFile IS HLittleFsFile, so no other kind of HIFile can reach
  // this function. This mirrors HFsDesktop::openFile() exactly - if a build
  // ever links two backends at once, both lines break together.
  HLittleFsFile& file = static_cast<HLittleFsFile&>(outFile);

  file.close();  // Defensive: never leak a previously-open handle in outFile.

  char resolved[HLITTLEFS_MAX_PATH_LEN];
  if (!mounted_ || mode == nullptr || !buildPath(path, resolved, sizeof(resolved))) {
    return false;
  }

  std::FILE* handle = std::fopen(resolved, mode);
  if (handle == nullptr) {
    return false;
  }

  file.handle_ = handle;
  return true;
}

bool HLittleFs::exists(const char* path) const {
  char resolved[HLITTLEFS_MAX_PATH_LEN];
  if (!mounted_ || !buildPath(path, resolved, sizeof(resolved))) {
    return false;
  }

  struct stat info;
  return stat(resolved, &info) == 0;
}

bool HLittleFs::isDirectory(const char* path) const {
  char resolved[HLITTLEFS_MAX_PATH_LEN];
  if (!mounted_ || !buildPath(path, resolved, sizeof(resolved))) {
    return false;
  }

  struct stat info;
  return stat(resolved, &info) == 0 && S_ISDIR(info.st_mode);
}

bool HLittleFs::listDir(const char* path, const HFsEntryVisitor& visitor) const {
  if (!mounted_ || path == nullptr || !visitor.is_valid()) {
    return false;
  }

  char resolved[HLITTLEFS_MAX_PATH_LEN];

  // buildPath() rejects an empty path, which is right for every other call
  // here - there is no file called "" - but listing the root is a real
  // operation, and the root is the mount point itself.
  if (path[0] == '\0' || strcmp(path, "/") == 0) {
    snprintf(resolved, sizeof(resolved), "%s", HLITTLEFS_BASE_PATH);
  } else if (!buildPath(path, resolved, sizeof(resolved))) {
    return false;
  }

  DIR* dir = opendir(resolved);
  if (dir == nullptr) {
    return false;
  }

  bool stopped = false;
  const struct dirent* entry = readdir(dir);
  while (entry != nullptr && !stopped) {
    // Neither is ever produced by esp_littlefs, but readdir() is a POSIX
    // interface and a caller walking a tree must not be handed its own parent.
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
      stopped = !visitor(entry->d_name, entry->d_type == DT_DIR);
    }
    if (!stopped) {
      entry = readdir(dir);
    }
  }

  closedir(dir);
  return true;
}

bool HLittleFs::deleteFile(const char* path) {
  char resolved[HLITTLEFS_MAX_PATH_LEN];
  if (!mounted_ || isDirectory(path) || !buildPath(path, resolved, sizeof(resolved))) {
    return false;  // Directories must go through removeDir().
  }
  return ::unlink(resolved) == 0;
}

bool HLittleFs::rename(const char* from, const char* to) {
  char resolvedFrom[HLITTLEFS_MAX_PATH_LEN];
  char resolvedTo[HLITTLEFS_MAX_PATH_LEN];
  if (!mounted_ ||
      !buildPath(from, resolvedFrom, sizeof(resolvedFrom)) ||
      !buildPath(to, resolvedTo, sizeof(resolvedTo))) {
    return false;
  }

  // ::rename, not this class's rename(). lfs_rename replaces an existing
  // destination and commits the swap through LittleFS's journal, which is the
  // atomicity HIFs::rename() demands and what HConfig's staging file relies
  // on. It must never be rewritten as unlink-then-rename.
  return ::rename(resolvedFrom, resolvedTo) == 0;
}

bool HLittleFs::createDir(const char* path) {
  char resolved[HLITTLEFS_MAX_PATH_LEN];
  if (!mounted_ || !buildPath(path, resolved, sizeof(resolved))) {
    return false;
  }
  if (exists(path)) {
    // Idempotent: an existing directory is success; a file at that path is a real conflict.
    return isDirectory(path);
  }
  return ::mkdir(resolved, 0777) == 0;
}

bool HLittleFs::removeDir(const char* path) {
  char resolved[HLITTLEFS_MAX_PATH_LEN];
  if (!mounted_ || !isDirectory(path) || !buildPath(path, resolved, sizeof(resolved))) {
    return false;
  }
  // rmdir refuses a non-empty directory, matching HFsDesktop::removeDir()'s
  // use of std::filesystem::remove rather than remove_all.
  return ::rmdir(resolved) == 0;
}
