#pragma once

#include <cstddef>
#include <string_view>

class HJsonDocument;

/**
 * @brief A single mutable JSON node (Object, Array, String, Int, Double, Bool or Null).
 *
 * HJsonValue never allocates from the system heap: every child node and
 * every string it owns is carved out of its parent HJsonDocument's bump
 * allocator. Children of an Object or Array are stored as an intrusive
 * singly-linked list (`head_` / `next_`) rather than a dynamic array,
 * so the DOM never needs to reallocate or move existing nodes.
 *
 * A node is only valid for as long as the HJsonDocument that allocated it
 * (and, for parsed strings, the pool memory it copied them into) remains
 * alive.
 */
class HJsonValue {
 public:
  /** @brief JSON value kinds this node can hold. */
  enum class Type {
    Null,
    Object,
    Array,
    String,
    Int,
    Double,
    Bool
  };

  /**
   * @brief Constructs a Null node.
   * @param doc Owning document, used to request memory for future mutations.
   *        May be nullptr for standalone/sentinel nodes that are never mutated.
   */
  explicit HJsonValue(HJsonDocument* doc = nullptr);

  /** @brief Returns this node's current type. */
  Type type() const;

  /** @brief Returns true if this node is Type::Null (including a missing-value sentinel). */
  bool isNull() const;

  /** @brief Returns the number of direct children (0 for non-collection types). */
  size_t size() const;

  /**
   * @brief Read-only lookup of an Object member by key (Null Object Pattern).
   * @param key Null-terminated key to search for.
   * @return The matching child, or a shared dummy Null node if not found -
   *         safe to chain (e.g. `doc["a"]["b"].asInt()`) without crashing.
   */
  const HJsonValue& operator[](const char* key) const;

  /**
   * @brief Mutable lookup of an Object member by key, auto-vivifying it.
   *
   * If this node is Null, it is first promoted to an Object. If `key`
   * already exists, its node is returned as-is. Otherwise a new child is
   * allocated from the owning document's pool, linked in, and returned.
   * @param key Null-terminated key to search for or create.
   * @return The child node, or the shared dummy Null node if this node's
   *         type conflicts (already a non-Object, non-Null type) or the
   *         pool is exhausted.
   */
  HJsonValue& operator[](const char* key);

  /**
   * @brief Read-only lookup of an Array element by position (Null Object Pattern).
   *
   * Deliberately named `at()` rather than `operator[](size_t)`: a class
   * that overloads operator[] for both `const char*` and an integral type
   * makes `x[0]` ambiguous (0 is a null pointer constant, so it resolves
   * to *both* candidates), which is a well-known C++ footgun. Naming this
   * `at()` sidesteps it entirely.
   * @param index Zero-based element index.
   * @return The element, or a shared dummy Null node if out of range.
   */
  const HJsonValue& at(size_t index) const;

  /**
   * @brief Appends a new element to this Array, auto-vivifying it.
   *
   * If this node is Null, it is first promoted to an Array. Existing
   * elements are left untouched; the new element is always linked at the
   * tail so element order matches append order.
   * @return The new element node, or the shared dummy Null node if this
   *         node's type conflicts or the pool is exhausted.
   */
  HJsonValue& pushBack();

  /** @brief Interprets this node as an int (numeric/bool types coerce; others return 0). */
  int asInt() const;

  /** @brief Interprets this node as a double (numeric/bool types coerce; others return 0.0). */
  double asDouble() const;

  /** @brief Interprets this node as a bool (numeric types coerce via != 0; others return false). */
  bool asBool() const;

  /** @brief Returns a view of this node's string text, or an empty view if not a String. */
  std::string_view asString() const;

  /** @brief Overwrites this node in place as an Int. */
  HJsonValue& operator=(int value);

  /** @brief Overwrites this node in place as a Double. */
  HJsonValue& operator=(double value);

  /** @brief Overwrites this node in place as a Bool. */
  HJsonValue& operator=(bool value);

  /** @brief Overwrites this node in place as a String; the text is copied into the pool. */
  HJsonValue& operator=(const char* value);

 private:
  /** @brief Returns the shared, process-wide dummy Null node used by the Null Object Pattern. */
  static const HJsonValue& nullValue();

  /** @brief Linear search of this Object's children for `key`; nullptr if not an Object or not found. */
  HJsonValue* findChild(const char* key) const;

  /** @brief Walks this Array's children to the `index`-th element; nullptr if not an Array or out of range. */
  HJsonValue* findElement(size_t index) const;

  /**
   * @brief Appends `child` to the tail of this node's children list.
   *
   * The list only stores a head pointer (no tail cache), so this walks to
   * the end on every call. That is an accepted O(n) cost per insertion for
   * the small, embedded-config-sized documents HJson targets.
   */
  void linkChild(HJsonValue* child);

  // HJsonDocument needs direct access to type_/value_/head_/next_/key_ to
  // build and serialize the DOM during parse()/serialize(), and to call
  // linkChild() while parsing - see HJsonDocument.cpp.
  friend class HJsonDocument;

  /** @brief Backing storage for the scalar value kinds. Only one member is active at a time, per type_. */
  union Storage {
    int intValue;
    double doubleValue;
    bool boolValue;
    const char* stringValue;  // Always a pool-owned, null-terminated copy.
  };

  Type type_;
  Storage value_;
  HJsonValue* next_;     // Next sibling in the parent's children list.
  HJsonValue* head_;     // First child, for Object/Array types.
  const char* key_;      // Pool-owned key text; non-null only for Object members.
  HJsonDocument* doc_;   // Owning document, used to allocate on mutation.
};
