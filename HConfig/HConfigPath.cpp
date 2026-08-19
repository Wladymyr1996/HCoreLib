#include "HConfig/HConfigPath.hpp"

#include <cstdio>
#include <cstring>

HConfigPath::HConfigPath() : valid_(true) {
}

HConfigPath::HConfigPath(const char* text) : valid_(true) {
  parse(text);
}

bool HConfigPath::makeSegment(const char* text, size_t length, Segment& outSegment) {
  if (text == nullptr || length == 0 || length > HCONFIG_MAX_CONFIG_NAME_LEN) {
    return false;
  }

  memcpy(outSegment.text, text, length);
  outSegment.text[length] = '\0';

  // All-digits means array subscript. The width check keeps the accumulator
  // inside uint16_t without needing a range test per digit: five digits can
  // still overflow, so the value is clamped below rather than wrapping.
  outSegment.numeric = true;
  uint32_t value = 0;
  for (size_t i = 0; i < length; ++i) {
    const char c = outSegment.text[i];
    if (c < '0' || c > '9') {
      outSegment.numeric = false;
      break;
    }
    value = (value * 10) + static_cast<uint32_t>(c - '0');
    if (value > 0xFFFF) {
      value = 0xFFFF;
    }
  }
  outSegment.value = outSegment.numeric ? static_cast<uint16_t>(value) : 0;
  return true;
}

bool HConfigPath::parse(const char* text) {
  clear();

  if (text == nullptr) {
    valid_ = false;
    return false;
  }

  const char* cursor = text;
  if (*cursor == '/') {
    ++cursor;  // A leading slash is optional: "travelMs" == "/travelMs".
  }

  while (*cursor != '\0') {
    const char* const slash = strchr(cursor, '/');
    const size_t length = (slash != nullptr) ? static_cast<size_t>(slash - cursor) : strlen(cursor);

    if (length == 0) {
      // A trailing slash is harmless and ends the path; an interior empty
      // segment ("/a//b") is a malformed path and must not silently collapse.
      if (slash != nullptr && slash[1] == '\0') {
        break;
      }
      clear();
      valid_ = false;
      return false;
    }

    Segment segment;
    if (!makeSegment(cursor, length, segment) || segments_.full()) {
      clear();
      valid_ = false;
      return false;
    }
    segments_.push_back(segment);

    if (slash == nullptr) {
      break;
    }
    cursor = slash + 1;
  }

  return true;
}

void HConfigPath::clear() {
  segments_.clear();
  valid_ = true;
}

bool HConfigPath::push(const char* segment) {
  if (segments_.full()) {
    return false;
  }

  Segment parsed;
  if (segment == nullptr || !makeSegment(segment, strlen(segment), parsed)) {
    return false;
  }
  segments_.push_back(parsed);
  return true;
}

bool HConfigPath::pushIndex(uint16_t indexValue) {
  char text[8];
  snprintf(text, sizeof(text), "%u", static_cast<unsigned>(indexValue));
  return push(text);
}

void HConfigPath::pop() {
  if (!segments_.empty()) {
    segments_.pop_back();
  }
}

size_t HConfigPath::size() const {
  return segments_.size();
}

bool HConfigPath::empty() const {
  return segments_.empty();
}

bool HConfigPath::valid() const {
  return valid_;
}

const char* HConfigPath::segment(size_t indexValue) const {
  return (indexValue < segments_.size()) ? segments_[indexValue].text : "";
}

bool HConfigPath::isIndex(size_t indexValue) const {
  return indexValue < segments_.size() && segments_[indexValue].numeric;
}

uint16_t HConfigPath::index(size_t indexValue) const {
  return isIndex(indexValue) ? segments_[indexValue].value : 0;
}

bool HConfigPath::operator==(const HConfigPath& other) const {
  if (segments_.size() != other.segments_.size()) {
    return false;
  }
  return commonPrefixLength(other) == segments_.size();
}

bool HConfigPath::operator!=(const HConfigPath& other) const {
  return !(*this == other);
}

size_t HConfigPath::commonPrefixLength(const HConfigPath& other) const {
  const size_t limit = (segments_.size() < other.segments_.size()) ? segments_.size()
                                                                   : other.segments_.size();
  size_t shared = 0;
  while (shared < limit && strcmp(segments_[shared].text, other.segments_[shared].text) == 0) {
    ++shared;
  }
  return shared;
}

bool HConfigPath::startsWith(const HConfigPath& prefix) const {
  if (prefix.segments_.size() > segments_.size()) {
    return false;
  }
  return commonPrefixLength(prefix) == prefix.segments_.size();
}

bool HConfigPath::isChildOf(const HConfigPath& parent) const {
  return segments_.size() == parent.segments_.size() + 1 && startsWith(parent);
}

int HConfigPath::compare(const HConfigPath& a, const HConfigPath& b) {
  const size_t limit = (a.segments_.size() < b.segments_.size()) ? a.segments_.size()
                                                                 : b.segments_.size();

  for (size_t i = 0; i < limit; ++i) {
    const Segment& left = a.segments_[i];
    const Segment& right = b.segments_[i];

    if (left.numeric && right.numeric) {
      // The reason this function exists rather than a plain strcmp: as text,
      // "10" sorts before "2", which would interleave array elements 2 and 10
      // and split a sibling run in half. The writer emits in one pass and
      // cannot recover from that.
      if (left.value != right.value) {
        return (left.value < right.value) ? -1 : 1;
      }
      continue;
    }

    // Indices before names, so that mixed shapes still have a total order.
    if (left.numeric != right.numeric) {
      return left.numeric ? -1 : 1;
    }

    const int diff = strcmp(left.text, right.text);
    if (diff != 0) {
      return diff;
    }
  }

  // A prefix sorts before what extends it - which is also the order the
  // writer needs, parents ahead of their children.
  if (a.segments_.size() != b.segments_.size()) {
    return (a.segments_.size() < b.segments_.size()) ? -1 : 1;
  }
  return 0;
}

bool HConfigPath::toText(char* outBuffer, size_t bufferSize) const {
  if (outBuffer == nullptr || bufferSize == 0) {
    return false;
  }

  outBuffer[0] = '\0';
  size_t used = 0;
  for (size_t i = 0; i < segments_.size(); ++i) {
    const size_t segmentLen = strlen(segments_[i].text);
    if (used + 1 + segmentLen + 1 > bufferSize) {
      outBuffer[0] = '\0';
      return false;
    }
    outBuffer[used++] = '/';
    memcpy(outBuffer + used, segments_[i].text, segmentLen);
    used += segmentLen;
  }
  outBuffer[used] = '\0';
  return true;
}
