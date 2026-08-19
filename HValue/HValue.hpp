#pragma once

// For <HCoreLibConfig.h>, so an application override of the limit below is
// actually visible here. Without this the #ifndef would only ever see whatever
// a previous include happened to define - which is to say nothing, silently,
// and the "tunable" would quietly not be one.
#include <HCoreLib.h>

#include <etl/string.h>
#include <etl/string_view.h>

/**
 * Maximum character capacity of the string storage inside every HValue.
 *
 * NOT a free knob. The buffer is inside the object, so the cost is paid by every
 * HValue that exists at once - a batch of HConfigEntry being written, a span of
 * results coming back from readMany(), anything an application keeps. Raising it
 * by N bytes costs N times all of those. Measure before raising it.
 */
#ifndef HVALUE_MAX_STRING_LEN
#define HVALUE_MAX_STRING_LEN 31
#endif

/**
 * @brief A strictly-typed, zero-heap-allocation value container.
 *
 * The unit configuration is stored and read in: one of five types, chosen once,
 * with no allocation anywhere.
 *
 * HValue holds exactly one of Null/Bool/Int/Float/String, and that type is
 * fixed for the object's entire lifetime: it is chosen once by whichever
 * constructor runs, and no operation afterward can change it. All five
 * assignment operators (scalar, string, copy, move) instead COERCE the
 * incoming value to fit the object's existing type - assigning "25.5" to
 * a Float HValue parses it and stores 25.5f; it never becomes a String.
 *
 * Memory layout: the etl::string<31> payload lives INSIDE the same union
 * as the scalar types rather than alongside it, so a Null/Bool/Int/Float
 * HValue only pays for the size of its own scalar plus the Type tag - not
 * an always-present ~32-byte string buffer it never uses. Because
 * etl::string has a non-trivial constructor/destructor, the union cannot
 * default-construct or destroy its members automatically; this class
 * placement-news the string member in exactly the constructors that need
 * it, and explicitly calls its destructor in ~HValue() (and nowhere else -
 * see the class-level note above `Storage`). etl::string itself never
 * allocates from the heap (fixed inline buffer), so this is all
 * stack/static memory throughout.
 *
 * The `as*()` getters never fail: if the current type can't be sensibly
 * converted to the requested one, they return a safe default
 * (0 / 0.0f / false / "") rather than asserting or throwing.
 */
class HValue {
 public:
  /** @brief The kind of value stored. Fixed for the object's whole lifetime. */
  enum class Type {
    Null,
    Bool,
    Int,
    Float,
    String
  };

  /** @brief Constructs a Null value. */
  HValue();

  /** @brief Constructs an Int value. */
  HValue(int value);

  /** @brief Constructs a Float value. */
  HValue(float value);

  /** @brief Constructs a Bool value. */
  HValue(bool value);

  /**
   * @brief Constructs a String value.
   * @param value Null-terminated text; nullptr is treated as an empty
   *        string. Text longer than HVALUE_MAX_STRING_LEN is truncated.
   */
  HValue(const char* value);

  /**
   * @brief Constructs a String value from a view.
   * @param value Text longer than HVALUE_MAX_STRING_LEN is truncated.
   */
  HValue(etl::string_view value);

  /** @brief Copy-constructs a value of the same type as `other`. */
  HValue(const HValue& other);

  /** @brief Move-constructs a value of the same type as `other`. */
  HValue(HValue&& other) noexcept;

  /** @brief Destroys the payload (explicitly, if it is a live String). */
  ~HValue();

  /**
   * @brief Coerces `other`'s value into this object's existing type.
   *
   * type() is never changed by this call. The source value is read via
   * `other`'s own asBool()/asInt()/asFloat()/asString() - i.e. assigning a
   * Float HValue into an existing String HValue formats it as text, and
   * assigning a String HValue into an existing Int HValue parses it.
   */
  HValue& operator=(const HValue& other);

  /**
   * @brief Coerces `other`'s value into this object's existing type.
   *
   * Behaves identically to copy-assignment. A "move" cannot mean anything
   * more here: type() is immutable and can differ between `this` and
   * `other`, so a cross-type assignment always has to reformat/reparse
   * the value regardless of value category, and etl::string<31> has no
   * heap buffer to steal even when the types do match - "moving" it is no
   * cheaper than copying it. So this simply reuses the coercion path
   * rather than pretending to optimize a transfer that isn't possible.
   */
  HValue& operator=(HValue&& other) noexcept;

  /**
   * @brief Coerces `value` into this object's existing type (see class docs).
   *
   * Numeric/Bool destinations store `value` directly or via a trivial
   * cast; a String destination is overwritten with `value` formatted as text.
   */
  HValue& operator=(int value);

  /** @brief Coerces `value` into this object's existing type (see class docs). */
  HValue& operator=(float value);

  /** @brief Coerces `value` into this object's existing type (see class docs). */
  HValue& operator=(bool value);

  /**
   * @brief Coerces `value` into this object's existing type (see class docs).
   *
   * A String destination is overwritten with `value` (truncated if too
   * long); a numeric/Bool destination parses `value` the same way asInt()
   * / asFloat() / asBool() would. nullptr is treated as an empty string.
   */
  HValue& operator=(const char* value);

  /** @brief Coerces `value` into this object's existing type (see class docs). */
  HValue& operator=(etl::string_view value);

  /**
   * @brief STRICT equality: same type AND same value.
   *
   * There is deliberately NO coercion here, unlike everywhere else in this
   * class. `HValue(1) == HValue(1.0f)` is FALSE, and so is
   * `HValue(1) == HValue("1")` - a type mismatch short-circuits before the
   * payload is even looked at.
   *
   * That is the only rule consistent with the type being immutable: two
   * values of different types can never become one another, so calling them
   * equal would claim an interchangeability that does not exist. It is also
   * what makes a "did this actually change?" check cheap enough to run on every
   * write - a mismatched type costs one byte compare and never touches the
   * payload.
   *
   * Two Null values are equal (both carry no payload). Strings compare by
   * content.
   */
  bool operator==(const HValue& other) const;

  /** @brief Negation of operator==; see its docs for the strictness rule. */
  bool operator!=(const HValue& other) const;

  /** @brief Returns the type fixed at construction. */
  Type type() const;

  /** @brief Returns true if type() == Type::Null. */
  bool isNull() const;

  /** @brief Returns true if type() == Type::Bool. */
  bool isBool() const;

  /** @brief Returns true if type() == Type::Int. */
  bool isInt() const;

  /** @brief Returns true if type() == Type::Float. */
  bool isFloat() const;

  /** @brief Returns true if type() == Type::String. */
  bool isString() const;

  /**
   * @brief Coerces the current value to an int.
   *
   * Float truncates toward zero, Bool maps to 1/0. A String is parsed with
   * strtol (base 10), using its longest valid leading numeric prefix (e.g.
   * "42abc" -> 42); a String with no valid leading integer, and Null, both
   * yield 0.
   */
  int asInt() const;

  /**
   * @brief Coerces the current value to a float.
   *
   * Int converts directly, Bool maps to 1.0f/0.0f. A String is parsed with
   * strtof, using its longest valid leading numeric prefix; a String with
   * no valid leading number, and Null, both yield 0.0f.
   */
  float asFloat() const;

  /**
   * @brief Coerces the current value to a bool.
   *
   * Int/Float are true when non-zero. A String is true only for exactly
   * "true" or "1"; every other string (including "false", "0", or
   * garbage) is false, as is Null.
   */
  bool asBool() const;

  /**
   * @brief Coerces the current value to a string.
   *
   * Int/Float are formatted with snprintf ("%d" / "%g"); Bool becomes
   * "true"/"false"; Null becomes an empty string; String is returned as-is.
   */
  etl::string<HVALUE_MAX_STRING_LEN> asString() const;

 private:
  using StringType = etl::string<HVALUE_MAX_STRING_LEN>;

  /**
   * @brief Raw storage for whichever single type this HValue holds.
   *
   * A plain (non-anonymous) union so it can carry its own no-op
   * constructor/destructor: etl::string's non-trivial special members
   * would otherwise make the union's default constructor/destructor
   * implicitly deleted, and anonymous unions cannot declare member
   * functions at all. HValue itself is fully responsible for activating
   * (placement-new) and tearing down (explicit destructor call) whichever
   * member matches type_ - see the constructors, destroyPayload(), and
   * the class-level docs above.
   */
  union Storage {
    int intValue_;
    float floatValue_;
    bool boolValue_;
    StringType stringValue_;

    Storage() {}
    ~Storage() {}
  };

  static int parseInt(const StringType& text);
  static float parseFloat(const StringType& text);
  static bool parseBool(const StringType& text);
  static StringType formatInt(int value);
  static StringType formatFloat(float value);
  static StringType formatBool(bool value);

  /** @brief Explicitly destroys the active payload if it is a live String. */
  void destroyPayload();

  /** @brief Shared implementation for both copy- and move-assignment; see their docs. */
  void coerceAssign(const HValue& other);

  Type type_;
  Storage storage_;
};
