#pragma once

#include <cstddef>
#include <cstdint>

#include <etl/vector.h>

#include "HConfig/HConfigTypes.hpp"

/**
 * @brief A parsed, slash-separated config path: "/units/0/name".
 *
 * The one piece shared by all three parser modes and by the writer, because
 * all four are ultimately answering the same question - which node is this,
 * and how does it compare to the one I want?
 *
 * Fixed capacity throughout: at most HCONFIG_MAX_DEPTH segments, each at most
 * HCONFIG_MAX_CONFIG_NAME_LEN characters. A path that exceeds either is
 * REJECTED rather than truncated, and valid() says so. Truncating would be
 * worse than useless here: a shortened path is not an invalid path, it is a
 * DIFFERENT one, quite possibly a real one, and silently reading or writing
 * the wrong setting is the failure this class exists to prevent.
 *
 * A segment made entirely of digits is an ARRAY INDEX; anything else is an
 * object member name. That is the whole distinction - the file's `[o]` and
 * `[ao]` tags decide the shape, and paths just address it.
 *
 * The parser also builds paths incrementally with push()/pop() as it walks
 * in and out of nested blocks, which is why those are public alongside
 * parse().
 */
class HConfigPath {
 public:
  /** @brief Constructs an empty path (size() == 0), which addresses the root. */
  HConfigPath();

  /** @brief Constructs from text; check valid() afterwards. @see parse */
  explicit HConfigPath(const char* text);

  /**
   * @brief Replaces the contents by parsing `text`.
   *
   * A leading '/' is optional and a trailing one is ignored, so "travelMs",
   * "/travelMs" and "/travelMs/" are the same path. Empty interior segments
   * ("/a//b"), over-long segments, and more than HCONFIG_MAX_DEPTH segments
   * all fail.
   * @return true if the path is usable; false leaves the object empty and
   *         valid() false.
   */
  bool parse(const char* text);

  /** @brief Discards every segment, leaving the root path. */
  void clear();

  /**
   * @brief Appends one segment.
   * @return false (and changes nothing) if the path is already at
   *         HCONFIG_MAX_DEPTH or `segment` is empty or too long.
   */
  bool push(const char* segment);

  /** @brief Appends a numeric segment - how array elements are named. */
  bool pushIndex(uint16_t index);

  /** @brief Removes the last segment. Does nothing when already empty. */
  void pop();

  /** @brief Number of segments; 0 is the root. */
  size_t size() const;

  /** @brief True when size() == 0. */
  bool empty() const;

  /** @brief True unless the last parse() failed. */
  bool valid() const;

  /** @brief The text of segment `index`, or "" if out of range. */
  const char* segment(size_t index) const;

  /** @brief True if segment `index` is all digits, i.e. an array subscript. */
  bool isIndex(size_t index) const;

  /** @brief The numeric value of segment `index`; 0 if it is not an index. */
  uint16_t index(size_t index) const;

  /** @brief True if both paths have the same segments in the same order. */
  bool operator==(const HConfigPath& other) const;
  bool operator!=(const HConfigPath& other) const;

  /** @brief True if `prefix` is this path's leading segments (or equal to it). */
  bool startsWith(const HConfigPath& prefix) const;

  /** @brief True if this path is `parent` plus exactly one more segment. */
  bool isChildOf(const HConfigPath& parent) const;

  /** @brief How many leading segments the two paths share. */
  size_t commonPrefixLength(const HConfigPath& other) const;

  /**
   * @brief Orders two paths segment by segment, numeric segments NUMERICALLY.
   *
   * Numeric comparison is the point: lexicographically "10" precedes "2",
   * which would interleave array elements and break the writer's
   * one-pass emission. An index segment always sorts before a named one, so
   * the ordering is total.
   * @return <0, 0 or >0 in the usual strcmp sense.
   */
  static int compare(const HConfigPath& a, const HConfigPath& b);

  /**
   * @brief Renders the path back to "/a/b/c" text.
   * @return false if `outBuffer` is too small, in which case it is left empty.
   */
  bool toText(char* outBuffer, size_t bufferSize) const;

 private:
  /** @brief One segment: its text, plus the numeric form when it is an index. */
  struct Segment {
    char text[HCONFIG_MAX_CONFIG_NAME_LEN + 1];
    uint16_t value;   ///< Parsed number; meaningful only when numeric is true.
    bool numeric;
  };

  /** @brief Fills `outSegment` from `text`, detecting the all-digits case. */
  static bool makeSegment(const char* text, size_t length, Segment& outSegment);

  etl::vector<Segment, HCONFIG_MAX_DEPTH> segments_;
  bool valid_;
};
