#pragma once

#include <cstddef>

#include "HJson/HJsonPool.hpp"
#include "HJson/HJsonValue.hpp"

/**
 * @brief Root container for a mutable JSON DOM: owns the bump allocator,
 *        the parser, and the serializer.
 *
 * HJsonDocument takes a caller-owned buffer and never touches the system
 * heap: every HJsonValue node and every string the DOM owns is carved out of
 * that buffer by an HJsonPool - a strict bump (pointer-increment) allocator
 * that never frees individual allocations. Calling parse() again reclaims the
 * whole buffer at once and starts over.
 *
 * The pool is a member rather than something this class does itself, so that
 * the nodes can hold the ALLOCATOR instead of holding the document: a part of
 * a document that depends on the whole of it is a cycle, and there is nothing
 * about allocating memory that needs to know what a JSON tree is.
 *
 * Because of the fixed backing buffer (typically a few KB), prefer
 * static/global storage for HJsonDocument instances over placing them deep
 * in a call stack. HJsonValue references returned from this document
 * remain valid only as long as both the backing buffer and this
 * HJsonDocument stay alive.
 */
class HJsonDocument {
 public:
  /**
   * @brief Constructs a document backed by a caller-owned buffer.
   * @param buffer Raw memory the bump allocator carves nodes/strings from.
   *        Must outlive this document.
   * @param capacity Size of `buffer` in bytes.
   */
  HJsonDocument(char* buffer, size_t capacity);

  /**
   * @brief Bump-allocates `size` bytes from the backing buffer. @see HJsonPool
   * @return Pointer to the allocation, or nullptr if the buffer is exhausted.
   */
  void* allocate(size_t size);

  /** @brief Returns the document's root node (mutable). */
  HJsonValue& getRoot();

  /** @brief Returns the document's root node (read-only). */
  const HJsonValue& getRoot() const;

  /**
   * @brief Parses a null-terminated JSON string into the DOM.
   *
   * Reclaims the entire backing buffer first, so this can be called
   * repeatedly on the same document to parse a new document each time.
   * @param jsonString Null-terminated JSON text. Only needs to remain
   *        valid for the duration of this call - all text the DOM keeps
   *        (keys, strings) is copied into the pool.
   * @return true if parsing succeeded and every node fit in the buffer.
   */
  bool parse(const char* jsonString);

  /**
   * @brief Serializes the DOM to a JSON string.
   * @param outputBuffer Destination buffer.
   * @param outputSize Size of `outputBuffer` in bytes.
   * @return Number of bytes written, excluding the null terminator. Output
   *         is always null-terminated (when outputSize > 0) and never
   *         written past outputSize, even if the full document does not fit.
   */
  size_t serialize(char* outputBuffer, size_t outputSize) const;

  /** @brief Bytes currently used in the backing buffer. Mainly useful for tests/diagnostics. */
  size_t usedBytes() const;

  /** @brief Total capacity of the backing buffer. Mainly useful for tests/diagnostics. */
  size_t capacityBytes() const;

 private:
  /** @brief Allocates and placement-constructs a blank HJsonValue in the pool. */
  HJsonValue* allocateNode();

  /** @brief Copies `length` bytes of text into the pool and null-terminates it. */
  const char* copyString(const char* text, size_t length);

  static const char* skipWhitespace(const char* p);
  const char* parseValue(const char* p, HJsonValue* value);
  const char* parseObject(const char* p, HJsonValue* value);
  const char* parseArray(const char* p, HJsonValue* value);
  const char* parseString(const char* p, const char** outText);
  const char* parseNumber(const char* p, HJsonValue* value);
  const char* parseLiteral(const char* p, HJsonValue* value);

  size_t serializeValue(const HJsonValue& value, char* out, size_t outSize, size_t pos) const;
  static size_t appendChar(char* out, size_t outSize, size_t pos, char c);
  static size_t appendText(char* out, size_t outSize, size_t pos, const char* text, size_t length);
  static size_t appendEscapedText(char* out, size_t outSize, size_t pos, const char* text, size_t length);

  // Declared before root_, which is constructed with a pointer to it.
  HJsonPool pool_;
  HJsonValue root_;
};
