#pragma once

// For <HCoreLibConfig.h>; see the note in HValue.hpp. Every limit below is
// overridable from the application.
#include <HCoreLib.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>

#include <etl/string.h>

#include "HValue/HValue.hpp"

/** Maximum character capacity of a module name (`etl::string<11>`). */
#ifndef HCONFIG_MAX_MODULE_NAME_LEN
#define HCONFIG_MAX_MODULE_NAME_LEN 11
#endif

/** Maximum character capacity of one key - one path SEGMENT, not the whole path. */
#ifndef HCONFIG_MAX_CONFIG_NAME_LEN
#define HCONFIG_MAX_CONFIG_NAME_LEN 21
#endif

/**
 * Maximum nesting depth, and therefore the maximum number of segments in a
 * path. The root's direct children are depth 1.
 *
 * Cheap, unlike the knobs the old cached HConfig had: it costs one small
 * frame per level on the stack of whichever call is running, and nothing at
 * all when nothing is running.
 */
#ifndef HCONFIG_MAX_DEPTH
#define HCONFIG_MAX_DEPTH 4
#endif

/** Maximum characters in one assembled logical line, excluding the newline. */
#ifndef HCONFIG_MAX_LINE_LEN
#define HCONFIG_MAX_LINE_LEN 96
#endif

/** Maximum characters in a whole slash-separated path ("/units/0/name"). */
#ifndef HCONFIG_MAX_PATH_LEN
#define HCONFIG_MAX_PATH_LEN 63
#endif

/**
 * Maximum entries one readMany() or patch() call may carry.
 *
 * Both track which entries have been satisfied in a single machine word, so
 * this cannot exceed 32 without changing that. It bounds the CALL, not the
 * file: write() has no such limit, because it streams the caller's span and
 * needs no per-entry state at all.
 */
#ifndef HCONFIG_MAX_BULK_ENTRIES
#define HCONFIG_MAX_BULK_ENTRIES 32
#endif

static_assert(HCONFIG_MAX_BULK_ENTRIES <= 32,
              "HCONFIG_MAX_BULK_ENTRIES is tracked in a uint32_t bitmask");

/**
 * @brief The type tag written between brackets on every line of a config file.
 *
 * The four scalar tags carry a value. The container tags carry structure
 * only: they exist so the leaves below them have paths, and HConfig never
 * returns one as a value - see HConfig-implementation.md section 4.
 */
enum class HConfigTag : uint8_t {
  Invalid,      ///< Not a recognised tag; the line is malformed.
  Int,          ///< [i]
  Float,        ///< [f]
  Bool,         ///< [b]
  String,       ///< [s]
  Object,       ///< [o]  - keyed children
  ArrayInt,     ///< [ai] - keyless [i] children
  ArrayBool,    ///< [ab]
  ArrayFloat,   ///< [af]
  ArrayString,  ///< [as]
  ArrayArray,   ///< [aa] - keyless [a*] children
  ArrayObject   ///< [ao] - keyless [o] children
};

/**
 * @brief One (path, scalar) pair - the unit of both write() and patch().
 *
 * The same struct serves both because they are the same idea at different
 * scope: write() supplies every entry in the file, patch() supplies only the
 * ones that change. `value` is always a scalar; there is no container HValue
 * in this system to put here.
 *
 * Still an aggregate, so `HConfigEntry{"travelMs", HValue(4080)}` works. The
 * one special member it declares is copy-assignment, and it has to - see below.
 */
struct HConfigEntry {
  /** Slash-separated, leading slash optional: "travelMs", "/units/0/name". */
  etl::string<HCONFIG_MAX_PATH_LEN> path;

  HValue value;

  HConfigEntry() = default;
  HConfigEntry(const HConfigEntry&) = default;

  /**
   * @brief Builds an entry from a path and a value.
   *
   * `HConfigEntry{"lang", HValue("en")}` used to work without this, because a
   * struct with only defaulted constructors is still an aggregate in C++17. It
   * is NOT one in C++20, where the rule became "no user-DECLARED constructors" -
   * so the same line stops compiling the moment a translation unit is built to a
   * newer standard, which ESP-IDF does by default. Declaring the constructor
   * makes both spellings work at every standard.
   *
   * The value is copy-CONSTRUCTED, which adopts its type. Assigning it instead
   * would coerce it into the Null a default-built entry starts as - see
   * operator= below for the same trap.
   */
  HConfigEntry(const char* entryPath, const HValue& entryValue)
      : path(entryPath), value(entryValue) {
  }

  /**
   * @brief Copies `other` WHOLE, type included.
   *
   * The compiler-generated version is actively wrong here, which is why this
   * exists. It assigns member-wise, and `value = other.value` invokes
   * HValue::operator=, which by design COERCES the source into the type the
   * destination already holds. So assigning a String entry over an Int one
   * would keep the Int and store a parsed 0 - the path would move and the
   * value would not, silently.
   *
   * That is not hypothetical: any sort that shifts elements assigns entries
   * over one another, and a list mixing a `[s]` name with an `[i]` timeout is
   * the ordinary case. Destroy-then-placement-new adopts the source's type,
   * which is the only assignment that moves a value WITH its meaning.
   */
  HConfigEntry& operator=(const HConfigEntry& other) {
    if (this != &other) {
      path = other.path;
      value.~HValue();
      new (&value) HValue(other.value);
    }
    return *this;
  }
};

/** Buffer size for a built module path: "config/" + name + ".cfg" + margin. */
#define HCONFIG_PATH_BUFFER_SIZE 32

/**
 * @brief Builds `config/{module}.cfg`, tolerating a leading slash on `module`.
 *
 * The slash-stripping is not cosmetic. `read("/mymod", ...)` is an easy thing
 * to write when every PATH in this API starts with one, and it used to produce
 * `config//mymod.cfg`. Windows and POSIX both collapse that, so it works on
 * the desktop build and would keep working right up until the ESP32, where
 * LittleFS is under no obligation to. It also meant "mymod" and "/mymod" were
 * two different strings naming one file, which is the sort of thing that makes
 * a write() and a later read() disagree for no visible reason.
 *
 * @return false if the name is missing, empty, or too long for the buffer.
 */
inline bool hConfigModulePath(const char* module, char* outBuffer, size_t outSize) {
  if (module == nullptr || outBuffer == nullptr || outSize == 0) {
    return false;
  }
  while (*module == '/') {
    ++module;
  }
  if (*module == '\0') {
    return false;
  }

  const int written = snprintf(outBuffer, outSize, "config/%s.cfg", module);
  return written > 0 && static_cast<size_t>(written) < outSize;
}

/** @brief Maps the text between brackets to a tag; Invalid if unrecognised. */
inline HConfigTag hConfigTagFromText(const char* text, size_t length) {
  if (text == nullptr) {
    return HConfigTag::Invalid;
  }
  if (length == 1) {
    switch (text[0]) {
      case 'i': return HConfigTag::Int;
      case 'f': return HConfigTag::Float;
      case 'b': return HConfigTag::Bool;
      case 's': return HConfigTag::String;
      case 'o': return HConfigTag::Object;
      default: return HConfigTag::Invalid;
    }
  }
  if (length == 2 && text[0] == 'a') {
    switch (text[1]) {
      case 'i': return HConfigTag::ArrayInt;
      case 'b': return HConfigTag::ArrayBool;
      case 'f': return HConfigTag::ArrayFloat;
      case 's': return HConfigTag::ArrayString;
      case 'a': return HConfigTag::ArrayArray;
      case 'o': return HConfigTag::ArrayObject;
      default: return HConfigTag::Invalid;
    }
  }
  return HConfigTag::Invalid;
}

/** @brief The bracket text for a tag; "" for Invalid. */
inline const char* hConfigTagToText(HConfigTag tag) {
  switch (tag) {
    case HConfigTag::Int: return "i";
    case HConfigTag::Float: return "f";
    case HConfigTag::Bool: return "b";
    case HConfigTag::String: return "s";
    case HConfigTag::Object: return "o";
    case HConfigTag::ArrayInt: return "ai";
    case HConfigTag::ArrayBool: return "ab";
    case HConfigTag::ArrayFloat: return "af";
    case HConfigTag::ArrayString: return "as";
    case HConfigTag::ArrayArray: return "aa";
    case HConfigTag::ArrayObject: return "ao";
    default: return "";
  }
}

/** @brief True for Object and every array tag - i.e. a line with children, not a value. */
inline bool hConfigTagIsContainer(HConfigTag tag) {
  return tag == HConfigTag::Object || tag >= HConfigTag::ArrayInt;
}

/** @brief True for every array tag (children are keyless and positional). */
inline bool hConfigTagIsArray(HConfigTag tag) {
  return tag >= HConfigTag::ArrayInt;
}

/**
 * @brief The tag every element of an array must carry; Invalid if not an array.
 *
 * This is what lets the parser reject a heterogeneous array: an `[s]:` line
 * inside an `[ai]` block does not match and is skipped as malformed.
 */
inline HConfigTag hConfigTagElementOf(HConfigTag arrayTag) {
  switch (arrayTag) {
    case HConfigTag::ArrayInt: return HConfigTag::Int;
    case HConfigTag::ArrayBool: return HConfigTag::Bool;
    case HConfigTag::ArrayFloat: return HConfigTag::Float;
    case HConfigTag::ArrayString: return HConfigTag::String;
    case HConfigTag::ArrayObject: return HConfigTag::Object;
    // An [aa] element is itself an array, and the file says which kind, so
    // any array tag is accepted there - see HConfigParser::childTagMatches().
    case HConfigTag::ArrayArray: return HConfigTag::ArrayArray;
    default: return HConfigTag::Invalid;
  }
}

/** @brief The scalar tag matching an HValue's type; Invalid for Null. */
inline HConfigTag hConfigTagOfValue(const HValue& value) {
  switch (value.type()) {
    case HValue::Type::Int: return HConfigTag::Int;
    case HValue::Type::Float: return HConfigTag::Float;
    case HValue::Type::Bool: return HConfigTag::Bool;
    case HValue::Type::String: return HConfigTag::String;
    default: return HConfigTag::Invalid;
  }
}

/**
 * @brief Builds an HValue of `tag`'s type holding `text`.
 *
 * Construct-then-assign, never assign-onto-a-placeholder: HValue's type is
 * fixed at construction and its operator= COERCES into that existing type,
 * so this is the only order that yields a correctly-typed value. Getting it
 * backwards produces a silently Null result - the same trap HConfig's
 * original parseConfigLine() documents.
 */
inline HValue hConfigValueFromText(HConfigTag tag, const char* text) {
  const char* const safe = (text != nullptr) ? text : "";
  switch (tag) {
    case HConfigTag::Int: {
      HValue value(0);
      value = safe;
      return value;
    }
    case HConfigTag::Float: {
      HValue value(0.0f);
      value = safe;
      return value;
    }
    case HConfigTag::Bool: {
      HValue value(false);
      value = safe;
      return value;
    }
    case HConfigTag::String:
      return HValue(safe);
    default:
      return HValue();
  }
}
