#include "HJsonValue.hpp"

#include <cstring>
#include <new>

#include "HJsonPool.hpp"

HJsonValue::HJsonValue(HJsonPool* pool)
    : type_(Type::Null), value_(), next_(nullptr), head_(nullptr), key_(nullptr), pool_(pool) {
}

HJsonValue* HJsonValue::allocateIn(HJsonPool& pool) {
  void* mem = pool.allocate(sizeof(HJsonValue));
  if (mem == nullptr) {
    return nullptr;
  }
  // Placement-new: constructs into pool memory that is already reserved. This
  // is not a heap allocation - it just runs the constructor at that address.
  return new (mem) HJsonValue(&pool);
}

HJsonValue::Type HJsonValue::type() const {
  return type_;
}

bool HJsonValue::isNull() const {
  return type_ == Type::Null;
}

size_t HJsonValue::size() const {
  size_t count = 0;
  for (const HJsonValue* child = head_; child != nullptr; child = child->next_) {
    ++count;
  }
  return count;
}

const HJsonValue& HJsonValue::nullValue() {
  static const HJsonValue kNull;
  return kNull;
}

HJsonValue* HJsonValue::findChild(const char* key) const {
  if (type_ != Type::Object) {
    return nullptr;
  }

  // Flat, singly-linked walk: an intrusive list has no faster lookup than
  // O(n), which is fine for the small key counts typical of a config document.
  for (HJsonValue* child = head_; child != nullptr; child = child->next_) {
    if (child->key_ != nullptr && strcmp(child->key_, key) == 0) {
      return child;
    }
  }
  return nullptr;
}

HJsonValue* HJsonValue::findElement(size_t index) const {
  if (type_ != Type::Array) {
    return nullptr;
  }

  HJsonValue* child = head_;
  for (size_t i = 0; child != nullptr && i < index; ++i) {
    child = child->next_;
  }
  return child;
}

void HJsonValue::linkChild(HJsonValue* child) {
  child->next_ = nullptr;

  if (head_ == nullptr) {
    head_ = child;
    return;
  }

  // No tail pointer is kept, so appending walks the whole list. This keeps
  // HJsonValue small and matches the intrusive-list layout the spec calls
  // for; O(n) per insertion is an accepted trade-off for small documents.
  HJsonValue* tail = head_;
  while (tail->next_ != nullptr) {
    tail = tail->next_;
  }
  tail->next_ = child;
}

const HJsonValue& HJsonValue::operator[](const char* key) const {
  if (key == nullptr) {
    return nullValue();
  }
  const HJsonValue* found = findChild(key);
  return (found != nullptr) ? *found : nullValue();
}

HJsonValue& HJsonValue::operator[](const char* key) {
  if (key == nullptr || pool_ == nullptr) {
    return const_cast<HJsonValue&>(nullValue());
  }

  // A fresh/empty node auto-promotes to Object on first indexed access, so
  // chained mutation like root["a"]["b"] = 1 works even when "a" did not
  // exist yet. A node that already holds some other concrete type refuses
  // to silently coerce - that would discard whatever it held.
  if (type_ == Type::Null) {
    type_ = Type::Object;
  }
  if (type_ != Type::Object) {
    return const_cast<HJsonValue&>(nullValue());
  }

  HJsonValue* existing = findChild(key);
  if (existing != nullptr) {
    return *existing;
  }

  HJsonValue* child = allocateIn(*pool_);
  if (child == nullptr) {
    return const_cast<HJsonValue&>(nullValue());  // Pool exhausted: degrade gracefully instead of crashing.
  }
  child->key_ = pool_->copyString(key, strlen(key));
  linkChild(child);
  return *child;
}

const HJsonValue& HJsonValue::at(size_t index) const {
  const HJsonValue* found = findElement(index);
  return (found != nullptr) ? *found : nullValue();
}

HJsonValue& HJsonValue::pushBack() {
  if (pool_ == nullptr) {
    return const_cast<HJsonValue&>(nullValue());
  }

  if (type_ == Type::Null) {
    type_ = Type::Array;
  }
  if (type_ != Type::Array) {
    return const_cast<HJsonValue&>(nullValue());
  }

  HJsonValue* child = allocateIn(*pool_);
  if (child == nullptr) {
    return const_cast<HJsonValue&>(nullValue());
  }
  child->key_ = nullptr;
  linkChild(child);
  return *child;
}

int HJsonValue::asInt() const {
  switch (type_) {
    case Type::Int:
      return value_.intValue;
    case Type::Double:
      return static_cast<int>(value_.doubleValue);
    case Type::Bool:
      return value_.boolValue ? 1 : 0;
    default:
      return 0;
  }
}

double HJsonValue::asDouble() const {
  switch (type_) {
    case Type::Double:
      return value_.doubleValue;
    case Type::Int:
      return static_cast<double>(value_.intValue);
    case Type::Bool:
      return value_.boolValue ? 1.0 : 0.0;
    default:
      return 0.0;
  }
}

bool HJsonValue::asBool() const {
  switch (type_) {
    case Type::Bool:
      return value_.boolValue;
    case Type::Int:
      return value_.intValue != 0;
    case Type::Double:
      return value_.doubleValue != 0.0;
    default:
      return false;
  }
}

std::string_view HJsonValue::asString() const {
  if (type_ != Type::String || value_.stringValue == nullptr) {
    return std::string_view();
  }
  return std::string_view(value_.stringValue);
}

HJsonValue& HJsonValue::operator=(int value) {
  type_ = Type::Int;
  value_.intValue = value;
  return *this;
}

HJsonValue& HJsonValue::operator=(double value) {
  type_ = Type::Double;
  value_.doubleValue = value;
  return *this;
}

HJsonValue& HJsonValue::operator=(bool value) {
  type_ = Type::Bool;
  value_.boolValue = value;
  return *this;
}

HJsonValue& HJsonValue::operator=(const char* value) {
  type_ = Type::String;
  // Per spec: assigned strings are copied into the pool so their lifetime
  // no longer depends on whatever buffer the caller passed in.
  value_.stringValue =
      (pool_ != nullptr && value != nullptr) ? pool_->copyString(value, strlen(value)) : nullptr;
  return *this;
}
