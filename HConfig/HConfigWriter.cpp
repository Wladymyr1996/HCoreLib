#define HLOG_MODULE_NAME "HConfig"

#include "HConfig/HConfigWriter.hpp"

#include <cstdio>
#include <cstring>

#include "HLog/HLog.hpp"

namespace {

/**
 * The ONE staging file. See the class docs for why a single fixed name is
 * both sufficient and what makes crash recovery a one-line delete rather
 * than a directory scan.
 */
constexpr const char* kStagingPath = "config/~.tmp";

}  // namespace

HConfigWriter::HConfigWriter() : open_(false), havePrevious_(false) {
}

HConfigWriter::~HConfigWriter() {
  // An abandoned writer must not leave a half-built staging file behind for
  // the next boot to find; abort() is a no-op once commit() has run.
  abort();
}

const char* HConfigWriter::stagingPath() {
  return kStagingPath;
}

bool HConfigWriter::begin() {
  abort();

  // BINARY - see the matching note in HConfigParser::open(). Text mode would
  // turn every '\n' below into CRLF on Windows and LF on the MCU, so the same
  // entries would produce different bytes on different platforms and patch()
  // could not promise to leave untouched lines alone.
  if (!HFs::HFileSystem::openFile(kStagingPath, file_, "wb")) {
    HWarning("could not open %s for writing", kStagingPath);
    return false;
  }

  open_ = true;
  havePrevious_ = false;
  previous_.clear();
  return true;
}

bool HConfigWriter::active() const {
  return open_;
}

bool HConfigWriter::writeText(const char* text, size_t length) {
  if (!open_ || length == 0) {
    return open_;
  }
  return file_.write(text, length) == length;
}

bool HConfigWriter::emitIndent(size_t depth) {
  // Two spaces per level, always. Sixteen is four times HCONFIG_MAX_DEPTH's
  // default, and the parser rejects anything deeper long before this matters.
  static const char kSpaces[] = "                ";
  const size_t needed = depth * 2;
  if (needed == 0) {
    return true;
  }
  if (needed > sizeof(kSpaces) - 1) {
    return false;
  }
  return writeText(kSpaces, needed);
}

bool HConfigWriter::emitRaw(const char* text) {
  if (text == nullptr) {
    return false;
  }
  if (!writeText(text, strlen(text))) {
    return false;
  }
  return writeText("\n", 1);
}

bool HConfigWriter::emitLine(size_t depth, const char* key, HConfigTag tag,
                             const char* valueText) {
  if (!emitIndent(depth)) {
    return false;
  }

  // Keyless is not a special case so much as the array case: an element's
  // position is its identity, so writing a name for it would be wrong, not
  // merely redundant.
  if (key != nullptr && key[0] != '\0' && !writeText(key, strlen(key))) {
    return false;
  }

  char tagText[8];
  snprintf(tagText, sizeof(tagText), "[%s]:", hConfigTagToText(tag));
  if (!writeText(tagText, strlen(tagText))) {
    return false;
  }

  // A container line ends at the colon - its contents are the indented lines
  // below it, so there is nothing to put here.
  if (valueText != nullptr) {
    if (!writeText(" ", 1) || !writeText(valueText, strlen(valueText))) {
      return false;
    }
  }

  return writeText("\n", 1);
}

HConfigTag HConfigWriter::containerTagAt(const HConfigPath& path, size_t index,
                                         const HValue& leaf) {
  // The container at `index` is described entirely by what comes AFTER it:
  // how its children are named, and whether those children have children.
  const size_t childIndex = index + 1;
  if (!path.isIndex(childIndex)) {
    return HConfigTag::Object;   // named children
  }

  const size_t grandchildIndex = index + 2;
  if (grandchildIndex >= path.size()) {
    // The elements are the leaves themselves, so the array's element type is
    // the value's type.
    switch (leaf.type()) {
      case HValue::Type::Int: return HConfigTag::ArrayInt;
      case HValue::Type::Float: return HConfigTag::ArrayFloat;
      case HValue::Type::Bool: return HConfigTag::ArrayBool;
      case HValue::Type::String: return HConfigTag::ArrayString;
      default: return HConfigTag::Invalid;
    }
  }

  return path.isIndex(grandchildIndex) ? HConfigTag::ArrayArray : HConfigTag::ArrayObject;
}

bool HConfigWriter::emitEntry(const HConfigPath& path, const HValue& value) {
  if (!open_ || path.empty()) {
    return false;
  }

  const HConfigTag leafTag = hConfigTagOfValue(value);
  if (leafTag == HConfigTag::Invalid) {
    // A Null has no tag and therefore no representable line. Skipped rather
    // than written as an empty one, matching what the original serializer did
    // - and skipped BEFORE any header is emitted, so a Null leaf cannot leave
    // an orphaned container behind.
    return true;
  }

  const size_t shared = havePrevious_ ? previous_.commonPrefixLength(path) : 0;

  // Everything from the first differing segment up to (not including) the
  // leaf is a container this entry is the first to enter.
  for (size_t depth = shared; depth + 1 < path.size(); ++depth) {
    const HConfigTag tag = containerTagAt(path, depth, value);
    if (tag == HConfigTag::Invalid) {
      return false;
    }
    // A segment is written keyless exactly when it is an element of the array
    // one level up. `depth > 0` is not redundant: the ROOT is always an
    // object, so a top-level key that happens to be all digits ("0", a
    // plausible enough setting name) is a NAME, not a subscript. Without that
    // guard it would be emitted keyless and read back as malformed.
    const char* const key = (depth > 0 && path.isIndex(depth)) ? "" : path.segment(depth);
    if (!emitLine(depth, key, tag, nullptr)) {
      return false;
    }
  }

  const size_t leafDepth = path.size() - 1;
  const char* const leafKey =
      (leafDepth > 0 && path.isIndex(leafDepth)) ? "" : path.segment(leafDepth);
  const etl::string<HVALUE_MAX_STRING_LEN> text = value.asString();
  if (!emitLine(leafDepth, leafKey, leafTag, text.c_str())) {
    return false;
  }

  previous_ = path;
  havePrevious_ = true;
  return true;
}

bool HConfigWriter::commit(const char* module) {
  char target[HCONFIG_PATH_BUFFER_SIZE];
  if (!open_ || !hConfigModulePath(module, target, sizeof(target))) {
    // Checked BEFORE the staging file is closed, so an unusable module name
    // leaves nothing half-committed behind - abort() still owns the cleanup.
    return false;
  }

  file_.close();
  open_ = false;

  // THE committing step, and the only one. Everything before it was written
  // to a file nobody reads; everything after it is live. There is no moment
  // in between.
  if (!HFs::HFileSystem::rename(kStagingPath, target)) {
    HWarning("could not swap %s over %s; the original is untouched", kStagingPath, target);
    HFs::HFileSystem::deleteFile(kStagingPath);
    return false;
  }
  return true;
}

void HConfigWriter::abort() {
  if (!open_) {
    return;
  }
  file_.close();
  open_ = false;
  HFs::HFileSystem::deleteFile(kStagingPath);
}
