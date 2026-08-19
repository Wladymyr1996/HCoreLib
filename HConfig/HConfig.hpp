#pragma once

#include <cstddef>

#include <etl/span.h>
#include <etl/string.h>

#include "HConfig/HConfigTypes.hpp"
#include "HValue/HValue.hpp"

/**
 * @brief Stateless, crash-safe configuration storage. Paths in, scalars out.
 *
 * A class of static methods with NO state of any kind - no cache, no arena,
 * no staging buffer. Every call opens `config/{module}.cfg`, streams what it
 * needs, and closes it. The old RAM-resident cache is gone, and with it the
 * 70 KB of static footprint it cost at this application's settings.
 *
 * **O(1) memory in file size.** Read, bulk read, write and patch all use one
 * line buffer and a stack of at most HCONFIG_MAX_DEPTH path segments. A 10 KB
 * config file costs what a 100-byte one costs.
 *
 * **Scalars only.** A config file may nest - `[o]`, `[ao]`, `[ai]` and the
 * rest - but that structure exists purely to give leaves their paths. This
 * class never returns a sub-tree, and HValue knows nothing about it. Asking
 * for a container path (`/units/0`) yields the default, not a partial answer.
 * The reasoning, including the design that was built and rejected, is in
 * Docs/task-ai/HConfig-implementation.md section 4.
 *
 * **Crash-safe writes.** Every mutation builds `config/~.tmp` and renames it
 * over the live file, so a power cut leaves the whole old file or the whole
 * new one. init() discards an orphaned staging file left by an interrupted
 * write.
 *
 * @code
 *   HConfig::init();
 *
 *   const int ms = HConfig::read("door1", "travelMs", HValue(4000)).asInt();
 *
 *   const size_t units = HConfig::count("garage", "/units");
 *   const HValue name  = HConfig::read("garage", "/units/0/name", HValue(""));
 *
 *   const HConfigEntry entries[] = {          // MUST be sorted by path
 *     { "name",     HValue("Garage Door") },
 *     { "travelMs", HValue(4080) },
 *   };
 *   HConfig::write("door1", entries);
 * @endcode
 */
class HConfig {
 public:
  HConfig() = delete;

  /**
   * @brief Creates `config/` and discards any orphaned staging file.
   *
   * Call once at startup, BEFORE anything reads configuration. The staging
   * sweep is the crash-recovery half: a leftover `config/~.tmp` means power
   * was lost between building a new file and swapping it in, so the live file
   * was never touched and the orphan is by definition incomplete. Discarding
   * it is the whole of the recovery, and it needs no directory iteration
   * because there is exactly one possible orphan.
   */
  static void init();

  // -------------------------------------------------------------------------
  // Reading
  // -------------------------------------------------------------------------

  /**
   * @brief The scalar at `path`.
   *
   * Returns with the type the FILE declares, not `defaultValue`'s - a `[s]`
   * line comes back as a String even when the default is an Int. That matches
   * what the old cache did, and callers that want a particular type should
   * say so with asInt()/asString() rather than relying on the default's type.
   *
   * Parsing stops at the match, so a setting near the top of the file costs
   * one buffer of I/O.
   *
   * @param path Slash-separated; a leading slash is optional
   *        ("travelMs", "/units/0/name").
   * @return `defaultValue` if the file, the path, or a scalar there does not
   *         exist. A CONTAINER path is not an error - it simply holds no
   *         value, so the default comes back.
   */
  static HValue read(const char* module, const char* path, const HValue& defaultValue);

  /**
   * @brief Reads several scalars in ONE pass over the file.
   *
   * The answer to "stateless reads cost one file open each". Only
   * `entries[i].path` is used; seed `out[i]` with your default, which is left
   * untouched when the path is absent. A found value REPLACES the slot with
   * the file's declared type, exactly as read() does.
   *
   * @param out Must be at least as long as `entries`; shorter is rejected.
   * @return How many paths were found.
   */
  static size_t readMany(const char* module, etl::span<const HConfigEntry> entries,
                         etl::span<HValue> out);

  /** @brief True if `path` resolves to a scalar leaf (a container is false). */
  static bool has(const char* module, const char* path);

  /**
   * @brief Number of children of the container at `path`.
   *
   * The length of an array or the member count of an object; 0 if `path` is
   * absent or names a scalar. An empty path counts the file's top level.
   *
   * This is what makes arrays iterable without ever handing out a sub-tree:
   * ask how many, then read `/arr/0 ... /arr/n-1`, or feed the whole list to
   * readMany() for a single pass.
   */
  static size_t count(const char* module, const char* path);

  // -------------------------------------------------------------------------
  // Writing - both go through the staging file and the atomic swap
  // -------------------------------------------------------------------------

  /**
   * @brief Sorts `entries` in place into the order write() requires.
   *
   * write() demands sorted input and refuses anything else, because emitting
   * a nested file in one pass only works when siblings are contiguous. That
   * is easy to satisfy for a literal array and tedious for a list built at
   * runtime, so this does it for you - explicitly, at a call site you can
   * see, rather than hidden inside write() where every caller would pay for
   * it whether or not they needed it.
   *
   * Insertion sort: no recursion, no scratch buffer, no allocation, and
   * linear on input that is already ordered - which is the common case, since
   * most callers build their list in a sensible order anyway. Entry counts
   * here are handfuls, not thousands; anything cleverer would cost more in
   * code size than it could ever save in comparisons.
   *
   * Ordering is HConfigPath::compare, exactly the rule write() validates
   * against: segment by segment, with numeric segments compared NUMERICALLY
   * so `/a/2` precedes `/a/10`.
   *
   * Sorting does not make an invalid list valid. Duplicate paths and sparse
   * array indices are still rejected by write(), because both mean the caller
   * asked for something that cannot be represented rather than merely
   * something out of order.
   */
  static void sort(etl::span<HConfigEntry> entries);

  /**
   * @brief Replaces `config/{module}.cfg` with exactly these entries.
   *
   * Whatever is not listed is gone, which is also how a setting is deleted.
   * Container structure is derived from the paths - `/units/0/name` and
   * `/units/1/name` produce a `units [ao]:` block by themselves.
   *
   * @param entries MUST be sorted by path (numeric segments compared
   *        numerically) and free of duplicates. That ordering is what keeps
   *        siblings contiguous, which is what lets the file be written in one
   *        pass. It is VALIDATED, not assumed: an out-of-order entry fails
   *        the whole call with a log line naming it, rather than producing a
   *        file with a duplicated section. Entries carrying a Null value are
   *        skipped - a Null has no type tag and so no representable line.
   * @return false on bad ordering, an unparseable path, or any I/O failure -
   *         in every case leaving the existing file byte-identical.
   */
  static bool write(const char* module, etl::span<const HConfigEntry> entries);

  /**
   * @brief Rewrites just the leaves named by `entries`, preserving the rest.
   *
   * A one-pass streaming filter: every line that is not being changed is
   * copied through BYTE FOR BYTE, so comments, blank lines, spacing and even
   * lines the lexer rejects survive. The value is coerced through the tag the
   * file already declares, so a patch cannot retype a setting.
   *
   * Not free - a full read plus a full write plus a rename. Batch changes
   * into one call rather than calling this per setting.
   *
   * A path with ONE segment that is not already present is APPENDED at the
   * end. A deeper path that is not present is NOT created: inserting it would
   * mean writing at a position the filter has already streamed past, which
   * cannot be done in one pass without buffering the rest of the file. Use
   * write() for structural change.
   *
   * @return false if any path is unresolvable-and-nested, or on I/O failure.
   *         All-or-nothing: on false the file is byte-identical to before,
   *         because the swap is the only committing step. Fail-fast is
   *         deliberate - a caller that asked for two changes and silently got
   *         one has a bug it cannot see.
   */
  static bool patch(const char* module, etl::span<const HConfigEntry> entries);

  /** @brief Deletes `config/{module}.cfg` entirely. @return false if it could not be removed. */
  static bool remove(const char* module);

  /**
   * @brief Deletes EVERY file in `config/`. A factory reset of configuration.
   *
   * There is no registry of modules anywhere in this class - a module exists
   * because a file with its name does - so this is the only way to clear
   * configuration without already knowing every name that was ever written.
   * That is exactly the case it is for: a pin table or a link set that is
   * wrong, written by a firmware that no longer exists.
   *
   * Everything goes, including any orphaned staging file. The directory itself
   * stays, so init() need not run again.
   *
   * ## It does not undo anything already in RAM
   * A unit or boundary that has read its params holds them, and the next
   * saveConfig() writes them straight back. This is a **reboot-then-defaults**
   * operation, not a live one:
   *
   * @code
   *   HConfig::init();
   *   HConfig::removeAll();      // before anything registers
   *   esp_restart();             // or simply carry on: every default is fresh
   * @endcode
   *
   * Calling it before any unit registers is what makes the next boot come up
   * on its compiled-in defaults.
   *
   * @return false if the directory could not be listed, or if any file in it
   *         could not be deleted - having removed as many as it could. True
   *         when `config/` is empty on return, which includes it already
   *         having been so.
   */
  static bool removeAll();
};
