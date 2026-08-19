#pragma once

#include <cstddef>

/**
 * @brief The bump allocator a JSON document is built in. Knows nothing of JSON.
 *
 * Split out of HJsonDocument for one reason: a node has to allocate (a new
 * member, a copied string) and used to ask the DOCUMENT for the memory, which
 * made the part depend on the whole. It needs an ALLOCATOR, not a document, and
 * that is all this is - so the dependency now runs one way, and this class can
 * be reasoned about, and got wrong, entirely on its own.
 *
 * Strictly bump: the pointer only moves forward and there is no free(). Memory
 * comes back in one piece, by reset(), which is exactly what parsing a new
 * document into the same buffer wants. Nothing here touches the system heap:
 * the buffer belongs to the caller and must outlive the pool.
 */
class HJsonPool {
 public:
  /**
   * @brief Wraps a caller-owned buffer.
   * @param buffer Memory to carve allocations from. Must outlive this pool.
   * @param capacity Size of `buffer` in bytes.
   */
  HJsonPool(char* buffer, size_t capacity);

  /**
   * @brief Bump-allocates `size` bytes.
   *
   * Rounded up to `alignof(std::max_align_t)`, so any type can be
   * placement-constructed at the returned address.
   * @return The allocation, or nullptr when the buffer is exhausted - which is
   *         an ordinary answer here, not an error: a document that does not fit
   *         degrades to a failed parse rather than to a heap or a crash.
   */
  void* allocate(size_t size);

  /**
   * @brief Copies `length` bytes of text into the pool and null-terminates it.
   * @return The copy, or nullptr if it did not fit.
   */
  const char* copyString(const char* text, size_t length);

  /** @brief Reclaims everything at once. Every pointer handed out is dead after this. */
  void reset();

  /** @brief Bytes currently used. Mainly useful for tests and diagnostics. */
  size_t usedBytes() const;

  /** @brief Total capacity of the backing buffer. */
  size_t capacityBytes() const;

 private:
  char* buffer_;
  size_t capacity_;
  size_t used_;
};
