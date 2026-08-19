#define HLOG_MODULE_NAME "HConfig"

#include "HConfig/HConfigParser.hpp"

#include <cstdio>
#include <cstring>

#include "HLog/HLog.hpp"

HConfigParser::HConfigParser()
    : open_(false), chunkLen_(0), chunkPos_(0), eof_(true), lineNumber_(0) {
  line_[0] = '\0';
  key_[0] = '\0';
}

bool HConfigParser::open(const char* module) {
  close();

  char path[HCONFIG_PATH_BUFFER_SIZE];
  if (!hConfigModulePath(module, path, sizeof(path))) {
    HWarning("unusable module name");
    return false;
  }

  if (!HFs::HFileSystem::exists(path)) {
    // Not an error, and deliberately not reported as one. A module nobody has
    // configured yet has no file; the caller gets zero lines and therefore
    // its defaults, with no special case at the call site.
    open_ = true;
    eof_ = true;
    return true;
  }

  // BINARY, not "r". On Windows a text-mode stream rewrites line endings
  // underneath us, so a file with LF endings would come back through patch()
  // converted to CRLF - which would break the byte-for-byte preservation
  // patch() promises, on lines the caller never asked to touch. Binary mode
  // also makes the desktop backend behave exactly like LittleFS, which has no
  // text mode at all. CR is still stripped when assembling a line, so a
  // hand-edited CRLF file reads fine either way.
  if (!HFs::HFileSystem::openFile(path, file_, "rb")) {
    HWarning("could not open %s for reading", path);
    return false;
  }

  open_ = true;
  eof_ = false;
  chunkLen_ = 0;
  chunkPos_ = 0;
  lineNumber_ = 0;
  stack_.clear();
  containerPath_.clear();
  return true;
}

void HConfigParser::close() {
  if (file_.isOpen()) {
    file_.close();
  }
  open_ = false;
  eof_ = true;
  chunkLen_ = 0;
  chunkPos_ = 0;
  lineNumber_ = 0;
  line_[0] = '\0';
  stack_.clear();
  containerPath_.clear();
}

size_t HConfigParser::lineNumber() const {
  return lineNumber_;
}

bool HConfigParser::readLine() {
  if (eof_ && chunkPos_ >= chunkLen_) {
    return false;
  }

  size_t used = 0;
  bool sawAnything = false;
  bool overflowed = false;

  for (;;) {
    if (chunkPos_ >= chunkLen_) {
      if (eof_) {
        break;
      }
      chunkLen_ = file_.read(chunk_, sizeof(chunk_));
      chunkPos_ = 0;
      if (chunkLen_ == 0) {
        eof_ = true;
        break;
      }
    }

    const char c = chunk_[chunkPos_++];
    sawAnything = true;

    if (c == '\n') {
      break;
    }
    if (c == '\r') {
      continue;  // Tolerate CRLF, as the original loader did.
    }

    if (used < HCONFIG_MAX_LINE_LEN) {
      line_[used++] = c;
    } else {
      // Truncate and remember. Splitting an over-long line into two would
      // manufacture a second bogus line out of the first one's tail, which is
      // harder to diagnose than one reported line.
      overflowed = true;
    }
  }

  line_[used] = '\0';

  if (!sawAnything) {
    return false;
  }

  if (overflowed) {
    HWarning("config line %u exceeds %d characters and was truncated",
             static_cast<unsigned>(lineNumber_ + 1), HCONFIG_MAX_LINE_LEN);
  }

  ++lineNumber_;
  return true;
}

void HConfigParser::lexLine(Lexed& outLexed) {
  outLexed.kind = HConfigLineKind::Malformed;
  outLexed.level = 0;
  outLexed.key = "";
  outLexed.tag = HConfigTag::Invalid;
  outLexed.value = "";
  outLexed.valueOffset = 0;
  key_[0] = '\0';

  // ---- Indent -----------------------------------------------------------
  size_t pos = 0;
  size_t spaces = 0;
  while (line_[pos] == ' ') {
    ++spaces;
    ++pos;
  }

  if (line_[pos] == '\0') {
    outLexed.kind = HConfigLineKind::Blank;
    return;
  }
  if (line_[pos] == '#') {
    outLexed.kind = HConfigLineKind::Comment;
    return;
  }
  if (line_[pos] == '\t') {
    HWarning("config line %u is indented with a tab; only spaces are indentation",
             static_cast<unsigned>(lineNumber_));
    return;
  }
  if ((spaces % 2) != 0) {
    // An error, not a rounding. Accepting 3 spaces as "close enough to 2 or 4"
    // would let one stray keypress silently reparent a whole block.
    HWarning("config line %u has %u leading spaces; indentation must be a multiple of 2",
             static_cast<unsigned>(lineNumber_), static_cast<unsigned>(spaces));
    return;
  }

  const size_t level = spaces / 2;
  if (level >= HCONFIG_MAX_DEPTH) {
    HWarning("config line %u nests deeper than HCONFIG_MAX_DEPTH (%d)",
             static_cast<unsigned>(lineNumber_), HCONFIG_MAX_DEPTH);
    return;
  }
  outLexed.level = static_cast<uint8_t>(level);

  // ---- Key --------------------------------------------------------------
  const size_t keyStart = pos;
  while (line_[pos] != '\0' && line_[pos] != '[') {
    ++pos;
  }
  if (line_[pos] != '[') {
    HWarning("config line %u has no type tag", static_cast<unsigned>(lineNumber_));
    return;
  }

  size_t keyEnd = pos;
  while (keyEnd > keyStart && line_[keyEnd - 1] == ' ') {
    --keyEnd;  // `name [s]:` and `name[s]:` are the same key.
  }
  const size_t keyLen = keyEnd - keyStart;
  if (keyLen > HCONFIG_MAX_CONFIG_NAME_LEN) {
    HWarning("config line %u has a key longer than %d characters",
             static_cast<unsigned>(lineNumber_), HCONFIG_MAX_CONFIG_NAME_LEN);
    return;
  }
  memcpy(key_, line_ + keyStart, keyLen);
  key_[keyLen] = '\0';
  outLexed.key = key_;

  // ---- Tag --------------------------------------------------------------
  ++pos;  // past '['
  const size_t tagStart = pos;
  while (line_[pos] != '\0' && line_[pos] != ']') {
    ++pos;
  }
  if (line_[pos] != ']') {
    HWarning("config line %u has an unterminated type tag", static_cast<unsigned>(lineNumber_));
    return;
  }
  outLexed.tag = hConfigTagFromText(line_ + tagStart, pos - tagStart);
  if (outLexed.tag == HConfigTag::Invalid) {
    HWarning("config line %u has an unrecognised type tag", static_cast<unsigned>(lineNumber_));
    return;
  }
  ++pos;  // past ']'

  // ---- Colon and value --------------------------------------------------
  if (line_[pos] != ':') {
    HWarning("config line %u is missing ':' after the type tag",
             static_cast<unsigned>(lineNumber_));
    return;
  }
  ++pos;
  while (line_[pos] == ' ') {
    ++pos;
  }

  outLexed.valueOffset = pos;
  outLexed.value = line_ + pos;
  outLexed.kind = HConfigLineKind::Node;
}

bool HConfigParser::childTagMatches(HConfigTag elemTag, HConfigTag childTag) {
  // An [aa] element is itself an array, and the child line says which kind -
  // [ai], [as], even another [aa]. Demanding an exact match there would make
  // arrays-of-arrays unusable, so any array tag is accepted.
  if (elemTag == HConfigTag::ArrayArray) {
    return hConfigTagIsArray(childTag);
  }
  return elemTag == childTag;
}

void HConfigParser::placeLine(const Lexed& lexed, Line& outLine) {
  outLine.kind = HConfigLineKind::Malformed;

  // 1. Leaving nested blocks is this loop, and nothing else. No lookahead,
  //    no recursion - the indentation already said where we are.
  while (stack_.size() > lexed.level) {
    stack_.pop_back();
    containerPath_.pop();
  }

  // 2. Indenting deeper than the previous line opened a container for.
  if (lexed.level > stack_.size()) {
    HWarning("config line %u is indented deeper than its parent allows",
             static_cast<unsigned>(lineNumber_));
    return;
  }

  const bool insideArray = !stack_.empty() && stack_.back().isArray;
  const bool hasKey = (lexed.key[0] != '\0');

  // 3. Shape: array children are keyless and positional, object children are
  //    named. Getting this backwards is the single most common hand-edit
  //    mistake, so it is checked explicitly rather than falling out of the
  //    path construction.
  if (insideArray) {
    if (hasKey) {
      HWarning("config line %u names an array element; elements are positional",
               static_cast<unsigned>(lineNumber_));
      return;
    }
    if (!childTagMatches(stack_.back().elemTag, lexed.tag)) {
      HWarning("config line %u declares [%s] inside an array of a different type",
               static_cast<unsigned>(lineNumber_), hConfigTagToText(lexed.tag));
      return;
    }
  } else if (!hasKey) {
    HWarning("config line %u has no key; only array elements may be keyless",
             static_cast<unsigned>(lineNumber_));
    return;
  }

  // 4. Name it. An array element's name is the parent's running counter, so
  //    numbering is a single increment rather than a search.
  outLine.path = containerPath_;
  if (insideArray) {
    if (!outLine.path.pushIndex(stack_.back().nextIndex)) {
      return;
    }
    ++stack_.back().nextIndex;
  } else if (!outLine.path.push(lexed.key)) {
    return;
  }

  outLine.kind = HConfigLineKind::Node;
  outLine.tag = lexed.tag;
  outLine.isContainer = hConfigTagIsContainer(lexed.tag);
  outLine.level = lexed.level;
  outLine.value = outLine.isContainer ? "" : lexed.value;
  outLine.valueOffset = lexed.valueOffset;

  // 5. Descend. A container with no children below it is simply an empty one.
  if (outLine.isContainer) {
    if (stack_.full()) {
      // Unreachable while lexLine() rejects level >= HCONFIG_MAX_DEPTH, but
      // the two limits are stated in different places, so this stays.
      HWarning("config line %u exceeds the nesting stack", static_cast<unsigned>(lineNumber_));
      outLine.kind = HConfigLineKind::Malformed;
      return;
    }
    Frame frame;
    frame.isArray = hConfigTagIsArray(lexed.tag);
    frame.nextIndex = 0;
    frame.elemTag = frame.isArray ? hConfigTagElementOf(lexed.tag) : HConfigTag::Invalid;
    stack_.push_back(frame);
    containerPath_ = outLine.path;
  }
}

bool HConfigParser::next(Line& outLine) {
  if (!open_ || !readLine()) {
    return false;
  }

  outLine.raw = line_;
  outLine.path.clear();
  outLine.tag = HConfigTag::Invalid;
  outLine.isContainer = false;
  outLine.level = 0;
  outLine.value = "";
  outLine.valueOffset = 0;

  Lexed lexed;
  lexLine(lexed);

  if (lexed.kind != HConfigLineKind::Node) {
    outLine.kind = lexed.kind;
    return true;
  }

  placeLine(lexed, outLine);
  return true;
}
