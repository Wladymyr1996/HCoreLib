#include "HFs/Desktop/HFsDesktopFile.hpp"

HFsDesktopFile::HFsDesktopFile() : handle_(nullptr) {
}

HFsDesktopFile::~HFsDesktopFile() {
  close();
}

size_t HFsDesktopFile::read(void* buffer, size_t size) {
  if (handle_ == nullptr) {
    return 0;
  }
  return std::fread(buffer, 1, size, handle_);
}

size_t HFsDesktopFile::write(const void* buffer, size_t size) {
  if (handle_ == nullptr) {
    return 0;
  }
  return std::fwrite(buffer, 1, size, handle_);
}

bool HFsDesktopFile::seek(size_t position) {
  if (handle_ == nullptr) {
    return false;
  }
  return std::fseek(handle_, static_cast<long>(position), SEEK_SET) == 0;
}

size_t HFsDesktopFile::size() const {
  if (handle_ == nullptr) {
    return 0;
  }

  // ftell/fseek round-trip that preserves the caller's current position:
  // stat()-ing the path instead would require re-deriving it, which this
  // class deliberately doesn't store once opened.
  const long current = std::ftell(handle_);
  if (current < 0) {
    return 0;
  }
  std::fseek(handle_, 0, SEEK_END);
  const long end = std::ftell(handle_);
  std::fseek(handle_, current, SEEK_SET);

  return (end > 0) ? static_cast<size_t>(end) : 0;
}

void HFsDesktopFile::close() {
  if (handle_ != nullptr) {
    std::fclose(handle_);
    handle_ = nullptr;
  }
}

bool HFsDesktopFile::isOpen() const {
  return handle_ != nullptr;
}
