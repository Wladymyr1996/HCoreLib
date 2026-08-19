#include "HValue/HValue.hpp"

#include <cstdio>
#include <cstdlib>
#include <new>
#include <utility>

HValue::HValue() : type_(Type::Null) {
  // Null carries no payload: no union member is ever activated for it, so
  // there is nothing to placement-new here and nothing to destroy later.
}

HValue::HValue(int value) : type_(Type::Int) {
  storage_.intValue_ = value;
}

HValue::HValue(float value) : type_(Type::Float) {
  storage_.floatValue_ = value;
}

HValue::HValue(bool value) : type_(Type::Bool) {
  storage_.boolValue_ = value;
}

HValue::HValue(const char* value) : type_(Type::String) {
  new (&storage_.stringValue_) StringType(value != nullptr ? value : "");
}

HValue::HValue(etl::string_view value) : type_(Type::String) {
  new (&storage_.stringValue_) StringType(value);
}

HValue::HValue(const HValue& other) : type_(other.type_) {
  // A fresh object: type_ is being set FROM other (per spec, constructors
  // set the type; only assignment coerces), so the active member always
  // matches other's, and String needs a real placement-new here - this
  // object's storage_.stringValue_ does not exist as a live object yet.
  switch (type_) {
    case Type::Bool:
      storage_.boolValue_ = other.storage_.boolValue_;
      break;

    case Type::Int:
      storage_.intValue_ = other.storage_.intValue_;
      break;

    case Type::Float:
      storage_.floatValue_ = other.storage_.floatValue_;
      break;

    case Type::String:
      new (&storage_.stringValue_) StringType(other.storage_.stringValue_);
      break;

    case Type::Null:
    default:
      break;
  }
}

HValue::HValue(HValue&& other) noexcept : type_(other.type_) {
  switch (type_) {
    case Type::Bool:
      storage_.boolValue_ = other.storage_.boolValue_;
      break;

    case Type::Int:
      storage_.intValue_ = other.storage_.intValue_;
      break;

    case Type::Float:
      storage_.floatValue_ = other.storage_.floatValue_;
      break;

    case Type::String:
      new (&storage_.stringValue_) StringType(std::move(other.storage_.stringValue_));
      break;

    case Type::Null:
    default:
      break;
  }
  // `other` is left holding a still-valid (if unspecified) string of the
  // same type_, exactly like any moved-from etl::string would be - type_
  // itself is immutable, so there is no "empty"/Null state to reset it to.
}

HValue::~HValue() {
  destroyPayload();
}

void HValue::destroyPayload() {
  if (type_ == Type::String) {
    storage_.stringValue_.~StringType();
  }
  // Bool/Int/Float/Null are trivially destructible - nothing to do, and
  // for Null no union member was ever activated in the first place.
}

void HValue::coerceAssign(const HValue& other) {
  // type_ never changes: pull other's value out through its own coercion
  // getters and store it via whichever member is already active for us.
  switch (type_) {
    case Type::Bool:
      storage_.boolValue_ = other.asBool();
      break;

    case Type::Int:
      storage_.intValue_ = other.asInt();
      break;

    case Type::Float:
      storage_.floatValue_ = other.asFloat();
      break;

    case Type::String:
      storage_.stringValue_ = other.asString();
      break;

    case Type::Null:
    default:
      break;  // Null has no payload slot to coerce into; assignment is a no-op.
  }
}

HValue& HValue::operator=(const HValue& other) {
  if (this != &other) {
    coerceAssign(other);
  }
  return *this;
}

HValue& HValue::operator=(HValue&& other) noexcept {
  if (this != &other) {
    coerceAssign(other);
  }
  return *this;
}

HValue& HValue::operator=(int value) {
  switch (type_) {
    case Type::Int:
      storage_.intValue_ = value;
      break;

    case Type::Float:
      storage_.floatValue_ = static_cast<float>(value);
      break;

    case Type::Bool:
      storage_.boolValue_ = (value != 0);
      break;

    case Type::String:
      storage_.stringValue_ = formatInt(value);
      break;

    case Type::Null:
    default:
      break;
  }
  return *this;
}

HValue& HValue::operator=(float value) {
  switch (type_) {
    case Type::Float:
      storage_.floatValue_ = value;
      break;

    case Type::Int:
      storage_.intValue_ = static_cast<int>(value);
      break;

    case Type::Bool:
      storage_.boolValue_ = (value != 0.0f);
      break;

    case Type::String:
      storage_.stringValue_ = formatFloat(value);
      break;

    case Type::Null:
    default:
      break;
  }
  return *this;
}

HValue& HValue::operator=(bool value) {
  switch (type_) {
    case Type::Bool:
      storage_.boolValue_ = value;
      break;

    case Type::Int:
      storage_.intValue_ = value ? 1 : 0;
      break;

    case Type::Float:
      storage_.floatValue_ = value ? 1.0f : 0.0f;
      break;

    case Type::String:
      storage_.stringValue_ = formatBool(value);
      break;

    case Type::Null:
    default:
      break;
  }
  return *this;
}

HValue& HValue::operator=(const char* value) {
  return *this = etl::string_view(value != nullptr ? value : "");
}

HValue& HValue::operator=(etl::string_view value) {
  switch (type_) {
    case Type::String:
      storage_.stringValue_ = value;
      break;

    case Type::Int:
      storage_.intValue_ = parseInt(StringType(value));
      break;

    case Type::Float:
      storage_.floatValue_ = parseFloat(StringType(value));
      break;

    case Type::Bool:
      storage_.boolValue_ = parseBool(StringType(value));
      break;

    case Type::Null:
    default:
      break;
  }
  return *this;
}

bool HValue::operator==(const HValue& other) const {
  // Strict by design - see the header. The type check comes first and is the
  // whole comparison for mismatched types, so the common "did this change?"
  // question costs a single byte compare in the worst case and never touches
  // the payload.
  if (type_ != other.type_) {
    return false;
  }

  switch (type_) {
    case Type::Bool:
      return storage_.boolValue_ == other.storage_.boolValue_;

    case Type::Int:
      return storage_.intValue_ == other.storage_.intValue_;

    case Type::Float:
      // Exact bit-for-bit comparison, deliberately: there is no epsilon that
      // is right for every consumer of this class, and OnChange notification
      // needs "the stored bits differ", not "the values differ meaningfully".
      // A caller that wants a tolerance owns that decision, not HValue.
      return storage_.floatValue_ == other.storage_.floatValue_;

    case Type::String:
      return storage_.stringValue_ == other.storage_.stringValue_;

    case Type::Null:
    default:
      // Two Nulls carry no payload at all, so they are always equal.
      return true;
  }
}

bool HValue::operator!=(const HValue& other) const {
  return !(*this == other);
}

HValue::Type HValue::type() const {
  return type_;
}

bool HValue::isNull() const {
  return type_ == Type::Null;
}

bool HValue::isBool() const {
  return type_ == Type::Bool;
}

bool HValue::isInt() const {
  return type_ == Type::Int;
}

bool HValue::isFloat() const {
  return type_ == Type::Float;
}

bool HValue::isString() const {
  return type_ == Type::String;
}

int HValue::asInt() const {
  switch (type_) {
    case Type::Int:
      return storage_.intValue_;

    case Type::Float:
      return static_cast<int>(storage_.floatValue_);

    case Type::Bool:
      return storage_.boolValue_ ? 1 : 0;

    case Type::String:
      return parseInt(storage_.stringValue_);

    default:
      return 0;
  }
}

float HValue::asFloat() const {
  switch (type_) {
    case Type::Float:
      return storage_.floatValue_;

    case Type::Int:
      return static_cast<float>(storage_.intValue_);

    case Type::Bool:
      return storage_.boolValue_ ? 1.0f : 0.0f;

    case Type::String:
      return parseFloat(storage_.stringValue_);

    default:
      return 0.0f;
  }
}

bool HValue::asBool() const {
  switch (type_) {
    case Type::Bool:
      return storage_.boolValue_;

    case Type::Int:
      return storage_.intValue_ != 0;

    case Type::Float:
      return storage_.floatValue_ != 0.0f;

    case Type::String:
      return parseBool(storage_.stringValue_);

    default:
      return false;
  }
}

HValue::StringType HValue::asString() const {
  switch (type_) {
    case Type::String:
      return storage_.stringValue_;

    case Type::Bool:
      return formatBool(storage_.boolValue_);

    case Type::Int:
      return formatInt(storage_.intValue_);

    case Type::Float:
      return formatFloat(storage_.floatValue_);

    default:
      return StringType();
  }
}

int HValue::parseInt(const StringType& text) {
  char* endPtr = nullptr;
  // strtol skips leading whitespace and stops at the first non-digit, so
  // "42abc" parses as 42. endPtr == c_str() means nothing at all was
  // consumed (e.g. "hello", "" or "abc123") - safe default 0.
  const long parsed = strtol(text.c_str(), &endPtr, 10);
  return (endPtr != text.c_str()) ? static_cast<int>(parsed) : 0;
}

float HValue::parseFloat(const StringType& text) {
  char* endPtr = nullptr;
  const float parsed = strtof(text.c_str(), &endPtr);
  return (endPtr != text.c_str()) ? parsed : 0.0f;
}

bool HValue::parseBool(const StringType& text) {
  // Deliberately strict: only the exact strings "true" and "1" count as
  // true. Everything else - "false", "0", "yes", garbage - is false,
  // rather than guessing at a broader truthy/falsy grammar.
  return text == "true" || text == "1";
}

HValue::StringType HValue::formatInt(int value) {
  char buffer[HVALUE_MAX_STRING_LEN + 1];
  snprintf(buffer, sizeof(buffer), "%d", value);
  return StringType(buffer);
}

HValue::StringType HValue::formatFloat(float value) {
  char buffer[HVALUE_MAX_STRING_LEN + 1];
  snprintf(buffer, sizeof(buffer), "%g", value);
  return StringType(buffer);
}

HValue::StringType HValue::formatBool(bool value) {
  return StringType(value ? "true" : "false");
}
