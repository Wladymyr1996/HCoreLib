#include "HJson/HJsonDocument.hpp"

#include <charconv>
#include <cstring>
#include <new>

HJsonDocument::HJsonDocument(char* buffer, size_t capacity)
    : buffer_(buffer), capacity_(capacity), used_(0), root_(this) {
}

void* HJsonDocument::allocate(size_t size) {
  // Bump allocator: every allocation just advances a pointer, never frees
  // individually. Round up to a universally safe alignment so any type -
  // including HJsonValue itself, placement-constructed below - can be
  // built at the returned address without UB on stricter architectures.
  const size_t alignment = alignof(std::max_align_t);
  const size_t alignedSize = (size + alignment - 1) & ~(alignment - 1);

  if (used_ + alignedSize > capacity_) {
    return nullptr;  // Out of memory: caller must handle gracefully.
  }

  void* ptr = buffer_ + used_;
  used_ += alignedSize;
  return ptr;
}

HJsonValue* HJsonDocument::allocateNode() {
  void* mem = allocate(sizeof(HJsonValue));
  if (mem == nullptr) {
    return nullptr;
  }
  // Placement-new: constructs into pool memory this document already
  // owns. This is not a heap allocation - it just runs HJsonValue's
  // constructor at an address the bump allocator already reserved.
  return new (mem) HJsonValue(this);
}

const char* HJsonDocument::copyString(const char* text, size_t length) {
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

HJsonValue& HJsonDocument::getRoot() {
  return root_;
}

const HJsonValue& HJsonDocument::getRoot() const {
  return root_;
}

size_t HJsonDocument::usedBytes() const {
  return used_;
}

size_t HJsonDocument::capacityBytes() const {
  return capacity_;
}

// ---------------- Parsing ----------------

const char* HJsonDocument::skipWhitespace(const char* p) {
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
    ++p;
  }
  return p;
}

bool HJsonDocument::parse(const char* jsonString) {
  if (jsonString == nullptr) {
    return false;
  }

  // Reclaim the whole pool and blank the root so this document can be
  // reused for a fresh parse without leaking stale state (or dangling
  // head_/next_ pointers into the now-reused pool) from a previous one.
  used_ = 0;
  root_ = HJsonValue(this);

  const char* p = parseValue(jsonString, &root_);
  if (p == nullptr) {
    return false;
  }

  p = skipWhitespace(p);
  return *p == '\0';  // Reject trailing garbage after the root value.
}

const char* HJsonDocument::parseValue(const char* p, HJsonValue* value) {
  p = skipWhitespace(p);

  switch (*p) {
    case '\0':
      return nullptr;  // Unexpected end of input.

    case '{':
      return parseObject(p, value);

    case '[':
      return parseArray(p, value);

    case '"': {
      const char* text = nullptr;
      p = parseString(p, &text);
      if (p == nullptr) {
        return nullptr;
      }
      value->type_ = HJsonValue::Type::String;
      value->value_.stringValue = text;
      return p;
    }

    case 't':
    case 'f':
    case 'n':
      return parseLiteral(p, value);

    default:
      if (*p == '-' || (*p >= '0' && *p <= '9')) {
        return parseNumber(p, value);
      }
      return nullptr;  // Unexpected character.
  }
}

const char* HJsonDocument::parseObject(const char* p, HJsonValue* value) {
  value->type_ = HJsonValue::Type::Object;
  value->head_ = nullptr;

  ++p;  // Consume '{'.
  p = skipWhitespace(p);

  if (*p == '}') {
    return p + 1;  // Empty object.
  }

  for (;;) {
    p = skipWhitespace(p);
    if (*p != '"') {
      return nullptr;  // Expected a key string.
    }

    const char* key = nullptr;
    p = parseString(p, &key);
    if (p == nullptr) {
      return nullptr;
    }

    p = skipWhitespace(p);
    if (*p != ':') {
      return nullptr;
    }
    ++p;  // Consume ':'.

    HJsonValue* child = allocateNode();
    if (child == nullptr) {
      return nullptr;  // Pool exhausted.
    }
    child->key_ = key;

    p = parseValue(p, child);
    if (p == nullptr) {
      return nullptr;
    }

    // Flat, singly-linked append: HJsonValue::linkChild walks to the tail
    // so object keys stay in source order (accessible here via friendship).
    value->linkChild(child);

    p = skipWhitespace(p);
    if (*p == ',') {
      ++p;
      continue;
    }
    if (*p == '}') {
      return p + 1;
    }
    return nullptr;  // Malformed: expected ',' or '}'.
  }
}

const char* HJsonDocument::parseArray(const char* p, HJsonValue* value) {
  value->type_ = HJsonValue::Type::Array;
  value->head_ = nullptr;

  ++p;  // Consume '['.
  p = skipWhitespace(p);

  if (*p == ']') {
    return p + 1;  // Empty array.
  }

  for (;;) {
    HJsonValue* child = allocateNode();
    if (child == nullptr) {
      return nullptr;  // Pool exhausted.
    }
    child->key_ = nullptr;

    p = parseValue(p, child);
    if (p == nullptr) {
      return nullptr;
    }

    value->linkChild(child);

    p = skipWhitespace(p);
    if (*p == ',') {
      ++p;
      continue;
    }
    if (*p == ']') {
      return p + 1;
    }
    return nullptr;  // Malformed: expected ',' or ']'.
  }
}

const char* HJsonDocument::parseString(const char* p, const char** outText) {
  ++p;  // Consume opening quote.
  const char* start = p;

  // First pass: find the matching closing quote, validating escapes just
  // enough to skip over them correctly, without decoding yet.
  while (*p != '\0' && *p != '"') {
    if (*p == '\\') {
      ++p;
      if (*p == '\0') {
        return nullptr;  // Dangling escape at end of input.
      }
      if (*p == 'u') {
        for (int i = 0; i < 4; ++i) {
          ++p;
          if (*p == '\0') {
            return nullptr;
          }
        }
      }
    }
    ++p;
  }
  if (*p != '"') {
    return nullptr;  // Unterminated string.
  }
  const char* end = p;
  ++p;  // Consume closing quote.

  // Second pass: decode escapes directly into a pool allocation sized for
  // the worst case (the raw span length). Decoding only ever shrinks or
  // preserves length - e.g. \n collapses two source chars into one, and
  // \uXXXX is passed through literally rather than re-encoded - so this
  // allocation is always big enough, and no temporary stack buffer is
  // needed to stage the decode.
  const size_t spanLength = static_cast<size_t>(end - start);
  char* decoded = static_cast<char*>(allocate(spanLength + 1));
  if (decoded == nullptr) {
    return nullptr;  // Pool exhausted.
  }

  size_t writeIndex = 0;
  for (const char* c = start; c < end; ++c) {
    if (*c != '\\') {
      decoded[writeIndex++] = *c;
      continue;
    }
    ++c;
    switch (*c) {
      case '"': decoded[writeIndex++] = '"'; break;
      case '\\': decoded[writeIndex++] = '\\'; break;
      case '/': decoded[writeIndex++] = '/'; break;
      case 'b': decoded[writeIndex++] = '\b'; break;
      case 'f': decoded[writeIndex++] = '\f'; break;
      case 'n': decoded[writeIndex++] = '\n'; break;
      case 'r': decoded[writeIndex++] = '\r'; break;
      case 't': decoded[writeIndex++] = '\t'; break;
      case 'u':
        // Validated above but passed through literally: full \uXXXX ->
        // UTF-8 decoding is out of scope for this embedded config parser.
        decoded[writeIndex++] = '\\';
        decoded[writeIndex++] = 'u';
        for (int i = 1; i <= 4; ++i) {
          decoded[writeIndex++] = *(c + i);
        }
        c += 4;
        break;
      default:
        return nullptr;  // Invalid escape sequence.
    }
  }
  decoded[writeIndex] = '\0';

  *outText = decoded;
  return p;
}

const char* HJsonDocument::parseNumber(const char* p, HJsonValue* value) {
  const char* start = p;

  if (*p == '-') {
    ++p;
  }
  while (*p >= '0' && *p <= '9') {
    ++p;
  }

  bool isDouble = false;
  if (*p == '.') {
    isDouble = true;
    ++p;
    while (*p >= '0' && *p <= '9') {
      ++p;
    }
  }
  if (*p == 'e' || *p == 'E') {
    isDouble = true;
    ++p;
    if (*p == '+' || *p == '-') {
      ++p;
    }
    while (*p >= '0' && *p <= '9') {
      ++p;
    }
  }

  if (p == start || (p == start + 1 && *start == '-')) {
    return nullptr;  // No digits consumed.
  }

  if (isDouble) {
    double result = 0.0;
    std::from_chars(start, p, result);
    value->type_ = HJsonValue::Type::Double;
    value->value_.doubleValue = result;
  } else {
    int result = 0;
    std::from_chars(start, p, result);
    value->type_ = HJsonValue::Type::Int;
    value->value_.intValue = result;
  }
  return p;
}

const char* HJsonDocument::parseLiteral(const char* p, HJsonValue* value) {
  if (strncmp(p, "true", 4) == 0) {
    value->type_ = HJsonValue::Type::Bool;
    value->value_.boolValue = true;
    return p + 4;
  }
  if (strncmp(p, "false", 5) == 0) {
    value->type_ = HJsonValue::Type::Bool;
    value->value_.boolValue = false;
    return p + 5;
  }
  if (strncmp(p, "null", 4) == 0) {
    value->type_ = HJsonValue::Type::Null;
    return p + 4;
  }
  return nullptr;
}

// ---------------- Serialization ----------------

size_t HJsonDocument::appendChar(char* out, size_t outSize, size_t pos, char c) {
  if (pos < outSize) {
    out[pos] = c;
  }
  return pos + 1;
}

size_t HJsonDocument::appendText(char* out, size_t outSize, size_t pos, const char* text, size_t length) {
  if (text == nullptr) {
    return pos;
  }
  for (size_t i = 0; i < length; ++i) {
    pos = appendChar(out, outSize, pos, text[i]);
  }
  return pos;
}

size_t HJsonDocument::appendEscapedText(char* out, size_t outSize, size_t pos, const char* text, size_t length) {
  if (text == nullptr) {
    return pos;
  }
  for (size_t i = 0; i < length; ++i) {
    const char c = text[i];
    switch (c) {
      case '"': pos = appendText(out, outSize, pos, "\\\"", 2); break;
      case '\\': pos = appendText(out, outSize, pos, "\\\\", 2); break;
      case '\n': pos = appendText(out, outSize, pos, "\\n", 2); break;
      case '\r': pos = appendText(out, outSize, pos, "\\r", 2); break;
      case '\t': pos = appendText(out, outSize, pos, "\\t", 2); break;
      default: pos = appendChar(out, outSize, pos, c); break;
    }
  }
  return pos;
}

size_t HJsonDocument::serializeValue(const HJsonValue& value, char* out, size_t outSize, size_t pos) const {
  switch (value.type_) {
    case HJsonValue::Type::Null:
      return appendText(out, outSize, pos, "null", 4);

    case HJsonValue::Type::Bool:
      return value.value_.boolValue
                 ? appendText(out, outSize, pos, "true", 4)
                 : appendText(out, outSize, pos, "false", 5);

    case HJsonValue::Type::Int: {
      char digits[16];
      const auto result = std::to_chars(digits, digits + sizeof(digits), value.value_.intValue);
      return appendText(out, outSize, pos, digits, static_cast<size_t>(result.ptr - digits));
    }

    case HJsonValue::Type::Double: {
      char digits[32];
      const auto result = std::to_chars(digits, digits + sizeof(digits), value.value_.doubleValue);
      const size_t length = static_cast<size_t>(result.ptr - digits);

      // Force a fractional marker so a later parse re-derives Type::Double
      // instead of Type::Int (to_chars renders a whole number like 5.0 as
      // the bare digits "5", which would otherwise round-trip as an Int).
      bool hasFraction = false;
      for (size_t i = 0; i < length; ++i) {
        if (digits[i] == '.' || digits[i] == 'e' || digits[i] == 'E') {
          hasFraction = true;
          break;
        }
      }
      pos = appendText(out, outSize, pos, digits, length);
      return hasFraction ? pos : appendText(out, outSize, pos, ".0", 2);
    }

    case HJsonValue::Type::String: {
      const char* text = (value.value_.stringValue != nullptr) ? value.value_.stringValue : "";
      pos = appendChar(out, outSize, pos, '"');
      pos = appendEscapedText(out, outSize, pos, text, strlen(text));
      return appendChar(out, outSize, pos, '"');
    }

    case HJsonValue::Type::Array: {
      pos = appendChar(out, outSize, pos, '[');
      bool first = true;
      for (const HJsonValue* child = value.head_; child != nullptr; child = child->next_) {
        if (!first) {
          pos = appendChar(out, outSize, pos, ',');
        }
        first = false;
        pos = serializeValue(*child, out, outSize, pos);
      }
      return appendChar(out, outSize, pos, ']');
    }

    case HJsonValue::Type::Object: {
      pos = appendChar(out, outSize, pos, '{');
      bool first = true;
      for (const HJsonValue* child = value.head_; child != nullptr; child = child->next_) {
        if (!first) {
          pos = appendChar(out, outSize, pos, ',');
        }
        first = false;
        pos = appendChar(out, outSize, pos, '"');
        pos = appendEscapedText(out, outSize, pos, child->key_, strlen(child->key_));
        pos = appendChar(out, outSize, pos, '"');
        pos = appendChar(out, outSize, pos, ':');
        pos = serializeValue(*child, out, outSize, pos);
      }
      return appendChar(out, outSize, pos, '}');
    }
  }

  return pos;  // Unreachable; keeps -Wreturn-type happy for the enum switch.
}

size_t HJsonDocument::serialize(char* outputBuffer, size_t outputSize) const {
  if (outputBuffer == nullptr || outputSize == 0) {
    return 0;
  }

  size_t pos = serializeValue(root_, outputBuffer, outputSize, 0);
  if (pos > outputSize - 1) {
    pos = outputSize - 1;  // Clamp to what actually fits, reserving room for '\0'.
  }
  outputBuffer[pos] = '\0';
  return pos;
}
