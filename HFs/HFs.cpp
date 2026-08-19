#include "HFs/HFs.hpp"

namespace HFs {

namespace {

// The one and only backend object in the process. A file-scope static, not a
// heap allocation and not a function-local static: its only state is a bool
// that starts false, so this avoids both the guard variable a function-local
// static would emit and any static-initialization order concern - both
// backends are constant-initialized.
#if IS_MCU
HLittleFs backend;
#else
HFsDesktop backend;
#endif

}  // namespace

HIFs& instance() {
  return backend;
}

bool HFileSystem::mount() {
  return instance().mount();
}

bool HFileSystem::createFile(const char* path) {
  return instance().createFile(path);
}

bool HFileSystem::openFile(const char* path, HIFile& outFile, const char* mode) {
  return instance().openFile(path, outFile, mode);
}

bool HFileSystem::deleteFile(const char* path) {
  return instance().deleteFile(path);
}

bool HFileSystem::rename(const char* from, const char* to) {
  return instance().rename(from, to);
}

bool HFileSystem::createDir(const char* path) {
  return instance().createDir(path);
}

bool HFileSystem::removeDir(const char* path) {
  return instance().removeDir(path);
}

bool HFileSystem::exists(const char* path) {
  return instance().exists(path);
}

bool HFileSystem::isDirectory(const char* path) {
  return instance().isDirectory(path);
}

bool HFileSystem::listDir(const char* path, const HFsEntryVisitor& visitor) {
  return instance().listDir(path, visitor);
}

}  // namespace HFs
