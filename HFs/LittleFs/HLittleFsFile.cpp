#include "HFs/LittleFs/HLittleFsFile.hpp"

HLittleFsFile::HLittleFsFile() : handle_(nullptr) {
}

HLittleFsFile::~HLittleFsFile() {
  close();
}

size_t HLittleFsFile::read(void* buffer, size_t size) {
  if (handle_ == nullptr) {
    return 0;
  }
  return std::fread(buffer, 1, size, handle_);
}

size_t HLittleFsFile::write(const void* buffer, size_t size) {
  if (handle_ == nullptr) {
    return 0;
  }
  return std::fwrite(buffer, 1, size, handle_);
}

bool HLittleFsFile::seek(size_t position) {
  if (handle_ == nullptr) {
    return false;
  }
  return std::fseek(handle_, static_cast<long>(position), SEEK_SET) == 0;
}

size_t HLittleFsFile::size() const {
  if (handle_ == nullptr) {
    return 0;
  }

  // ftell/fseek round-trip that preserves the caller's current position,
  // matching HFsDesktopFile. stat()-ing the path instead would require
  // re-deriving it, which this class deliberately doesn't store once opened.
  const long current = std::ftell(handle_);
  if (current < 0) {
    return 0;
  }
  std::fseek(handle_, 0, SEEK_END);
  const long end = std::ftell(handle_);
  std::fseek(handle_, current, SEEK_SET);

  return (end > 0) ? static_cast<size_t>(end) : 0;
}

void HLittleFsFile::close() {
  if (handle_ != nullptr) {
    // fclose flushes newlib's buffer into the VFS, which is what actually
    // reaches LittleFS. A handle that is written and never closed leaves the
    // tail of the file in RAM, so nothing here may skip it.
    std::fclose(handle_);
    handle_ = nullptr;
  }
}

bool HLittleFsFile::isOpen() const {
  return handle_ != nullptr;
}
