#pragma once

#include <cstddef>
#include <cstdint>

#include <etl/vector.h>

#include "HConfig/HConfigPath.hpp"
#include "HConfig/HConfigTypes.hpp"
#include "HFs/HFs.hpp"

/** @brief What one line of a config file turned out to be. */
enum class HConfigLineKind : uint8_t {
  Node,       ///< A real `key[tag]: value` line; `path`/`tag` are meaningful.
  Comment,    ///< First non-space character is '#'.
  Blank,      ///< Nothing but whitespace.
  Malformed   ///< Rejected by the lexer or by the shape rules; see HConfigParser.
};

/**
 * @brief Streams a config file one line at a time, tracking each line's path.
 *
 * An ITERATOR rather than a mode-driven engine, which is what keeps the three
 * things HConfig does with a file - seek one value, collect many, filter into
 * a rewrite - as three plain loops over the same source instead of three
 * variants of one parser. `patch()` needs the untouched original text of every
 * line it is not changing, so every line comes back including comments, blanks
 * and malformed ones, tagged with its kind.
 *
 * @code
 *   HConfigParser parser;
 *   parser.open("garage");            // missing file is fine: zero lines
 *   HConfigParser::Line line;
 *   while (parser.next(line)) {
 *     if (line.kind == HConfigLineKind::Node && !line.isContainer) {
 *       // line.path is the full path, e.g. /units/0/name
 *     }
 *   }
 *   parser.close();
 * @endcode
 *
 * MEMORY IS O(1) IN FILE SIZE, which is the whole design: one 64-byte read
 * buffer, one assembled line, and a stack of at most HCONFIG_MAX_DEPTH frames.
 * A 10 KB file costs exactly what a 100-byte one costs. Nothing accumulates,
 * because there is no tree to build - see HConfig-implementation.md section 4.
 *
 * Malformed lines are REPORTED, never fatal. One bad hand edit costs the
 * setting on that line, not the rest of the file.
 */
class HConfigParser {
 public:
  /** @brief One line of the file, as the parser understood it. */
  struct Line {
    HConfigLineKind kind;

    /** @brief Full path of this line's node, e.g. `/units/0/name`. Node lines only. */
    HConfigPath path;

    /** @brief The declared tag. Node lines only. */
    HConfigTag tag;

    /** @brief True when `tag` is `[o]` or an array tag - structure, not a value. */
    bool isContainer;

    /** @brief Nesting level, 0 for a top-level line. Node lines only. */
    uint8_t level;

    /** @brief The scalar text after the colon; "" for containers. Node lines only. */
    const char* value;

    /** @brief The line exactly as it was read, without its newline. Always valid. */
    const char* raw;

    /**
     * @brief Offset into `raw` where the value text begins.
     *
     * What lets patch() rewrite only the value and leave the indentation,
     * the key and the tag byte-identical - including whichever spelling of
     * `key[s]:` versus `key [s]:` the author used.
     */
    size_t valueOffset;
  };

  HConfigParser();

  HConfigParser(const HConfigParser&) = delete;
  HConfigParser& operator=(const HConfigParser&) = delete;

  /**
   * @brief Opens `config/{module}.cfg` for streaming.
   * @return false only if the file exists but cannot be opened. A MISSING
   *         file returns true and yields zero lines, because "no file yet" is
   *         a normal state for a module nobody has configured - callers get
   *         their defaults without special-casing it.
   */
  bool open(const char* module);

  /**
   * @brief Reads and classifies the next line.
   * @param outLine Filled in on success; its `raw`/`value` pointers are only
   *        valid until the following call to next().
   * @return false at end of file.
   */
  bool next(Line& outLine);

  /** @brief Closes the file. Safe on an already-closed parser. */
  void close();

  /** @brief 1-based number of the line last returned by next(); for diagnostics. */
  size_t lineNumber() const;

 private:
  /** @brief One open container: what its children are called and how many so far. */
  struct Frame {
    bool isArray;
    uint16_t nextIndex;   ///< Running element counter; arrays name children by it.
    HConfigTag elemTag;   ///< What each child must declare; arrays only.
  };

  /** @brief The per-line lexer's verdict before shape rules are applied. */
  struct Lexed {
    HConfigLineKind kind;
    uint8_t level;
    const char* key;      ///< Points into line_; "" for a keyless array element.
    HConfigTag tag;
    const char* value;
    size_t valueOffset;
  };

  /** @brief Assembles one logical line into line_, stripping CR. @return false at EOF. */
  bool readLine();

  /** @brief Runs the section 5.2 state machine over line_. */
  void lexLine(Lexed& outLexed);

  /** @brief Applies the nesting and keyed/keyless rules; fills outLine or marks it Malformed. */
  void placeLine(const Lexed& lexed, Line& outLine);

  /** @brief True if a child declaring `childTag` is legal inside an array of `elemTag`. */
  static bool childTagMatches(HConfigTag elemTag, HConfigTag childTag);

  HFs::HFile file_;
  bool open_;

  char chunk_[64];
  size_t chunkLen_;
  size_t chunkPos_;
  bool eof_;

  char line_[HCONFIG_MAX_LINE_LEN + 1];
  char key_[HCONFIG_MAX_CONFIG_NAME_LEN + 1];
  size_t lineNumber_;

  etl::vector<Frame, HCONFIG_MAX_DEPTH> stack_;

  /** @brief Keys of the currently open containers; always stack_.size() long. */
  HConfigPath containerPath_;
};
