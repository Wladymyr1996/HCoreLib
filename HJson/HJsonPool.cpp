#include "HJson/HJsonPool.hpp"

#include <cstring>

HJsonPool::HJsonPool(char* buffer, size_t capacity)
    : buffer_(buffer), capacity_(capacity), used_(0) {
}

void* HJsonPool::allocate(size_t size) {
  // Every allocation just advances a pointer and never frees individually.
  // Round up to a universally safe alignment so any type - including
  // HJsonValue, which is placement-constructed into this memory - can be built
  // at the returned address without UB on stricter architectures.
  const size_t alignment = alignof(std::max_align_t);
  const size_t alignedSize = (size + alignment - 1) & ~(alignment - 1);

  if (used_ + alignedSize > capacity_) {
    return nullptr;  // Out of memory: the caller must handle it gracefully.
  }

  void* ptr = buffer_ + used_;
  used_ += alignedSize;
  return ptr;
}

const char* HJsonPool::copyString(const char* text, size_t length) {
  if (text == nullptr) {
    return nullptr;
  }
  char* mem = static_cast<char*>(allocate(length + 1));
  if (mem == nullptr) {
    return nullptr;
  }
  memcpy(mem, text, length);
  mem[length] = '\0';
  return mem;
}

void HJsonPool::reset() {
  used_ = 0;
}

size_t HJsonPool::usedBytes() const {
  return used_;
}

size_t HJsonPool::capacityBytes() const {
  return capacity_;
}
