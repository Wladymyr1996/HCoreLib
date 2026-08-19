#pragma once

#include <cstddef>

#include "HConfig/HConfigPath.hpp"
#include "HConfig/HConfigTypes.hpp"
#include "HFs/HFs.hpp"

/**
 * @brief Builds a config file in a staging file and swaps it in atomically.
 *
 * EVERY mutation goes through this class, and every one of them is crash-safe
 * by construction. There is deliberately no "just write the real file"
 * shortcut: a half-written `config/door1.cfg` on a device that reads its
 * calibration at boot is the failure this whole design exists to prevent.
 *
 *   1. begin()   opens `config/~.tmp`
 *   2. emit...() streams the new contents into it
 *   3. commit()  closes it and renames it over the live file
 *
 * The rename is atomic with respect to power loss on both backends (see
 * HIFs::rename), so an interrupted write leaves the whole old file or the
 * whole new one - never a blend. An orphaned staging file is discarded by
 * HConfig::init() on the next boot.
 *
 * There is one staging name, not one per module, because config writes are
 * expected to be SERIALISED: there is never more than one in flight, so there
 * can never be more than one orphan, and cleanup needs no directory iteration.
 *
 * That assumption is load-bearing and worth knowing where it comes from. The
 * REST API answers on a single server task, so every write it causes is already
 * in order; start-up writes before that task exists. Two tasks writing at once
 * would corrupt each other's staging file - so a device that grows a second
 * writer needs a per-writer name or a lock, not a hope.
 *
 * NESTING IS DERIVED FROM THE PATHS. There is no tree object to walk, so
 * emitEntry() compares each path with the previous one and emits whatever
 * container headers the difference implies. That is only correct if entries
 * arrive in sorted order, which HConfig::write() enforces before calling here.
 */
class HConfigWriter {
 public:
  HConfigWriter();

  /** @brief Closes and discards the staging file if commit() was never reached. */
  ~HConfigWriter();

  HConfigWriter(const HConfigWriter&) = delete;
  HConfigWriter& operator=(const HConfigWriter&) = delete;

  /** @brief The one temporary file this module ever creates. */
  static const char* stagingPath();

  /** @brief Opens the staging file, truncating any leftover. @return false on I/O failure. */
  bool begin();

  /** @brief True between a successful begin() and a commit()/abort(). */
  bool active() const;

  /**
   * @brief Writes `text` followed by a newline, exactly as given.
   *
   * How patch() preserves everything it is not changing - comments, blank
   * lines, spacing, even lines the lexer rejected.
   */
  bool emitRaw(const char* text);

  /**
   * @brief Writes the leaf at `path`, preceded by any container headers the
   *        step from the previous path implies.
   *
   * The tag comes from `value`'s own type, and each container's tag is
   * inferred from the shape of the path below it: a numeric next segment
   * means an array, and whether the elements have segments of their own
   * decides `[ao]`/`[aa]` versus `[ai]`/`[as]`/...
   *
   * @param path Must be non-empty and must sort strictly after the previous
   *        one; the caller is responsible for that ordering.
   * @param value A scalar. A Null value writes nothing and returns true -
   *        it has no tag, so there is no line to write.
   */
  bool emitEntry(const HConfigPath& path, const HValue& value);

  /**
   * @brief Closes the staging file and renames it over `config/{module}.cfg`.
   *
   * The single committing step. On failure the staging file is removed and
   * the live file is left exactly as it was.
   */
  bool commit(const char* module);

  /** @brief Closes and deletes the staging file, changing nothing on disk. */
  void abort();

 private:
  /** @brief The container tag for segment `index` of `path`, given the leaf's value. */
  static HConfigTag containerTagAt(const HConfigPath& path, size_t index, const HValue& leaf);

  /** @brief Writes `2 * depth` spaces. */
  bool emitIndent(size_t depth);

  /**
   * @brief Writes one `key[tag]: value` line.
   * @param key nullptr or "" writes a keyless line - an array element.
   * @param valueText nullptr writes a container line, which carries no value.
   */
  bool emitLine(size_t depth, const char* key, HConfigTag tag, const char* valueText);

  bool writeText(const char* text, size_t length);

  HFs::HFile file_;
  bool open_;

  /** @brief The previous entry's path; what emitEntry() diffs against. */
  HConfigPath previous_;
  bool havePrevious_;
};
