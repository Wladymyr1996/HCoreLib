#define HLOG_MODULE_NAME "HConfig"

#include "HConfig/HConfig.hpp"

#include <cstdio>
#include <cstring>
#include <new>

#include <etl/vector.h>

#include "HConfig/HConfigParser.hpp"
#include "HConfig/HConfigPath.hpp"
#include "HConfig/HConfigWriter.hpp"
#include "HFs/HFs.hpp"
#include "HLog/HLog.hpp"

namespace {

/**
 * @brief Overwrites `slot` with `replacement`, ADOPTING its type.
 *
 * Deliberately not `slot = replacement`. HValue's type is immutable and its
 * operator= coerces into the type already there, so a plain assignment would
 * force every value readMany() produces into whatever type the caller happened
 * to seed - and a default-constructed Null slot would swallow the lot.
 * Destroying the slot and placement-newing the replacement over it is the only
 * way for readMany() to return the FILE's declared type the way read() does.
 */
void adoptValue(HValue& slot, const HValue& replacement) {
  slot.~HValue();
  new (&slot) HValue(replacement);
}

}  // namespace

void HConfig::init() {
  HFs::HFileSystem::createDir("config");

  // Crash recovery, in one line and with no directory scan. A leftover
  // staging file means power was lost between building a new config and
  // swapping it in: the live file was never touched, and the orphan is by
  // definition incomplete, so discarding it IS the recovery.
  if (HFs::HFileSystem::exists(HConfigWriter::stagingPath())) {
    HWarning("discarding %s left by an interrupted write", HConfigWriter::stagingPath());
    HFs::HFileSystem::deleteFile(HConfigWriter::stagingPath());
  }
}

HValue HConfig::read(const char* module, const char* path, const HValue& defaultValue) {
  HConfigPath wanted(path);
  // An empty path addresses the root, which is a container and holds no
  // scalar - the same answer as any other container path.
  if (!wanted.valid() || wanted.empty()) {
    return defaultValue;
  }

  HConfigParser parser;
  if (!parser.open(module)) {
    return defaultValue;
  }

  HConfigParser::Line line;
  while (parser.next(line)) {
    if (line.kind != HConfigLineKind::Node || line.isContainer) {
      continue;
    }
    if (line.path == wanted) {
      // Returned straight from here rather than accumulated: this is the
      // early exit that makes a setting near the top of the file cost one
      // buffer of I/O, and it is also the only way to return the file's own
      // type (see hConfigValueFromText).
      const HValue found = hConfigValueFromText(line.tag, line.value);
      parser.close();
      return found;
    }
  }

  parser.close();
  return defaultValue;
}

size_t HConfig::readMany(const char* module, etl::span<const HConfigEntry> entries,
                         etl::span<HValue> out) {
  if (out.size() < entries.size()) {
    HWarning("readMany(): out span holds %u slots for %u paths",
             static_cast<unsigned>(out.size()), static_cast<unsigned>(entries.size()));
    return 0;
  }
  if (entries.empty()) {
    return 0;
  }

  const size_t wantedCount = entries.size();
  if (wantedCount > HCONFIG_MAX_BULK_ENTRIES) {
    HWarning("readMany(): %u paths exceeds HCONFIG_MAX_BULK_ENTRIES (%d)",
             static_cast<unsigned>(wantedCount), HCONFIG_MAX_BULK_ENTRIES);
    return 0;
  }

  HConfigParser parser;
  if (!parser.open(module)) {
    return 0;
  }

  // One BIT per requested path, not a parsed-path array. Storing the parsed
  // paths would cost over 100 bytes each and put kilobytes on the stack of a
  // task that also runs every unit's update() - which would quietly undo the
  // O(1) footprint this whole module is built for. Reparsing a path is a
  // memcpy of at most HCONFIG_MAX_DEPTH short segments; the trade is not close.
  uint32_t filled = 0;
  size_t found = 0;

  HConfigParser::Line line;
  while (parser.next(line)) {
    if (line.kind != HConfigLineKind::Node || line.isContainer) {
      continue;
    }

    for (size_t i = 0; i < wantedCount; ++i) {
      const uint32_t bit = 1u << i;
      if ((filled & bit) != 0) {
        continue;  // Already satisfied - also how a duplicated path fills only its first slot.
      }
      const HConfigPath candidate(entries[i].path.c_str());
      if (!candidate.valid() || candidate != line.path) {
        continue;
      }

      adoptValue(out[i], hConfigValueFromText(line.tag, line.value));
      filled |= bit;
      ++found;
      break;
    }

    if (found == wantedCount) {
      break;  // Everything asked for has been seen; no reason to read on.
    }
  }

  parser.close();
  return found;
}

bool HConfig::has(const char* module, const char* path) {
  HConfigPath wanted(path);
  if (!wanted.valid() || wanted.empty()) {
    return false;
  }

  HConfigParser parser;
  if (!parser.open(module)) {
    return false;
  }

  HConfigParser::Line line;
  while (parser.next(line)) {
    if (line.kind == HConfigLineKind::Node && !line.isContainer && line.path == wanted) {
      parser.close();
      return true;
    }
  }

  parser.close();
  return false;
}

size_t HConfig::count(const char* module, const char* path) {
  HConfigPath wanted(path);
  if (!wanted.valid()) {
    return 0;
  }

  HConfigParser parser;
  if (!parser.open(module)) {
    return 0;
  }

  // Counting direct children by path rather than by watching a block open and
  // close: every line already carries its full path, so "is this a child of
  // the target" is one comparison and needs no extra state.
  size_t children = 0;
  HConfigParser::Line line;
  while (parser.next(line)) {
    if (line.kind == HConfigLineKind::Node && line.path.isChildOf(wanted)) {
      ++children;
    }
  }

  parser.close();
  return children;
}

void HConfig::sort(etl::span<HConfigEntry> entries) {
  // Straight insertion sort. Every assignment below moves a whole
  // HConfigEntry, which is exactly why that type declares its own
  // copy-assignment: the compiler-generated one would coerce each value into
  // the type of whatever slot it landed in, so shifting a String entry past
  // an Int one would quietly turn "Garage Door" into 0. See HConfigTypes.hpp.
  for (size_t i = 1; i < entries.size(); ++i) {
    const HConfigEntry key = entries[i];
    const HConfigPath keyPath(key.path.c_str());

    size_t j = i;
    while (j > 0 && HConfigPath::compare(HConfigPath(entries[j - 1].path.c_str()), keyPath) > 0) {
      entries[j] = entries[j - 1];
      --j;
    }
    entries[j] = key;
  }
}

bool HConfig::write(const char* module, etl::span<const HConfigEntry> entries) {
  if (module == nullptr) {
    return false;
  }

  // Validated BEFORE the staging file is opened, so a bad entry list costs no
  // I/O at all and cannot leave an orphan behind.
  HConfigPath previous;
  bool havePrevious = false;
  for (size_t i = 0; i < entries.size(); ++i) {
    const HConfigPath current(entries[i].path.c_str());
    if (!current.valid() || current.empty()) {
      HWarning("write(%s): entry %u has an unusable path '%s'", module, static_cast<unsigned>(i),
               entries[i].path.c_str());
      return false;
    }
    if (havePrevious && HConfigPath::compare(previous, current) >= 0) {
      HWarning("write(%s): entry %u ('%s') is out of order or duplicated; entries must be "
               "sorted by path", module, static_cast<unsigned>(i), entries[i].path.c_str());
      return false;
    }

    // Array indices must be DENSE and zero-based, and this is worth failing
    // over rather than tidying up. The file format stores an element's
    // position implicitly - its line order IS its index - so nothing records
    // that a caller skipped from 2 to 10. Writing a sparse array would
    // succeed, look right, and then read back renumbered from 0: values
    // silently at different paths than the ones just written. A caller that
    // means "these are elements 0 and 1" should say so.
    const size_t shared = havePrevious ? previous.commonPrefixLength(current) : 0;
    for (size_t depth = 0; depth < current.size(); ++depth) {
      if (!current.isIndex(depth)) {
        continue;
      }
      uint16_t expected = 0;
      if (depth < shared) {
        continue;  // Same element as the previous entry; already checked then.
      }
      if (havePrevious && depth == shared && depth < previous.size() &&
          previous.isIndex(depth)) {
        expected = static_cast<uint16_t>(previous.index(depth) + 1);
      }
      if (current.index(depth) != expected) {
        HWarning("write(%s): entry %u ('%s') uses array index %u where %u was expected; "
                 "indices must run 0,1,2... with no gaps", module, static_cast<unsigned>(i),
                 entries[i].path.c_str(), static_cast<unsigned>(current.index(depth)),
                 static_cast<unsigned>(expected));
        return false;
      }
    }

    previous = current;
    havePrevious = true;
  }

  HConfigWriter writer;
  if (!writer.begin()) {
    return false;
  }

  for (size_t i = 0; i < entries.size(); ++i) {
    const HConfigPath current(entries[i].path.c_str());
    if (!writer.emitEntry(current, entries[i].value)) {
      HWarning("write(%s): failed writing entry %u", module, static_cast<unsigned>(i));
      writer.abort();
      return false;
    }
  }

  return writer.commit(module);
}

bool HConfig::patch(const char* module, etl::span<const HConfigEntry> entries) {
  if (module == nullptr || entries.empty()) {
    return module != nullptr;
  }

  const size_t wantedCount = entries.size();
  if (wantedCount > HCONFIG_MAX_BULK_ENTRIES) {
    HWarning("patch(%s): %u entries exceeds HCONFIG_MAX_BULK_ENTRIES (%d)", module,
             static_cast<unsigned>(wantedCount), HCONFIG_MAX_BULK_ENTRIES);
    return false;
  }

  // Validated BEFORE any file is touched, so a bad path costs no I/O and can
  // leave no orphan. Like readMany(), the parsed forms are NOT kept - a bit
  // per entry is the entire per-call state; see the note there.
  for (size_t i = 0; i < wantedCount; ++i) {
    const HConfigPath current(entries[i].path.c_str());
    if (!current.valid() || current.empty()) {
      HWarning("patch(%s): entry %u has an unusable path '%s'", module, static_cast<unsigned>(i),
               entries[i].path.c_str());
      return false;
    }
  }

  uint32_t applied = 0;

  HConfigParser parser;
  if (!parser.open(module)) {
    return false;
  }

  HConfigWriter writer;
  if (!writer.begin()) {
    parser.close();
    return false;
  }

  uint32_t collided = 0;

  HConfigParser::Line line;
  while (parser.next(line)) {
    size_t match = wantedCount;
    if (line.kind == HConfigLineKind::Node) {
      for (size_t i = 0; i < wantedCount; ++i) {
        if ((applied & (1u << i)) != 0) {
          continue;
        }
        const HConfigPath candidate(entries[i].path.c_str());
        if (candidate != line.path) {
          continue;
        }
        if (line.isContainer) {
          // The path exists but names structure, not a value. Recorded rather
          // than ignored: falling through would leave it "unmatched", and a
          // one-segment unmatched path gets APPENDED - writing a scalar line
          // for a key the file already uses as a container, which is a
          // corrupt file that reads back as neither.
          collided |= (1u << i);
        } else {
          match = i;
        }
        break;
      }
    }

    if (match == wantedCount) {
      // Everything not being changed is copied through untouched - comments,
      // blanks, spacing, and lines the lexer rejected. This is the only
      // operation in the module that preserves a hand-edited file.
      if (!writer.emitRaw(line.raw)) {
        parser.close();
        writer.abort();
        return false;
      }
      continue;
    }

    // Only the VALUE is replaced. Everything before it - the indentation, the
    // key, whichever of `k[s]:` / `k [s]:` the author wrote - is copied from
    // the original bytes, so a patch changes exactly what it says it changes.
    // The value is coerced through the tag the FILE declares, so a patch can
    // never retype a setting.
    HValue typed = hConfigValueFromText(line.tag, "");
    typed = entries[match].value;
    const etl::string<HVALUE_MAX_STRING_LEN> text = typed.asString();

    char rebuilt[HCONFIG_MAX_LINE_LEN + HVALUE_MAX_STRING_LEN + 2];
    const size_t prefixLen = line.valueOffset;
    if (prefixLen + text.size() + 1 > sizeof(rebuilt)) {
      parser.close();
      writer.abort();
      return false;
    }
    memcpy(rebuilt, line.raw, prefixLen);
    memcpy(rebuilt + prefixLen, text.c_str(), text.size());
    rebuilt[prefixLen + text.size()] = '\0';

    if (!writer.emitRaw(rebuilt)) {
      parser.close();
      writer.abort();
      return false;
    }
    applied |= (1u << match);
  }

  parser.close();

  // Anything not found in the file: a top-level key can simply be appended,
  // but a nested one would have to be inserted at a position already streamed
  // past. Rather than buffer the rest of the file to do it - which would
  // throw away the O(1) memory this design is built on - the whole patch
  // fails and the caller is told exactly which path.
  for (size_t i = 0; i < wantedCount; ++i) {
    if ((applied & (1u << i)) != 0) {
      continue;
    }
    if ((collided & (1u << i)) != 0) {
      HWarning("patch(%s): '%s' names a container, not a value; there is nothing there to set",
               module, entries[i].path.c_str());
      writer.abort();
      return false;
    }
    const HConfigPath current(entries[i].path.c_str());
    if (current.size() > 1) {
      HWarning("patch(%s): '%s' does not exist and cannot be created in place; "
               "use write() for structural changes", module, entries[i].path.c_str());
      writer.abort();
      return false;
    }
    // Depth 1, so emitEntry() writes a bare top-level line and no container
    // header - which is exactly why appending is safe here and nesting is not.
    if (!writer.emitEntry(current, entries[i].value)) {
      writer.abort();
      return false;
    }
  }

  return writer.commit(module);
}

bool HConfig::remove(const char* module) {
  char path[HCONFIG_PATH_BUFFER_SIZE];
  if (!hConfigModulePath(module, path, sizeof(path))) {
    return false;
  }
  if (!HFs::HFileSystem::exists(path)) {
    return true;  // Idempotent: already absent is the requested state.
  }
  return HFs::HFileSystem::deleteFile(path);
}

bool HConfig::removeAll() {
  size_t removed = 0;

  // ONE FILE PER PASS, deliberately. Deleting an entry while its own directory
  // is being walked invalidates the iterator - LittleFS promises nothing about
  // it - and collecting every name first would need a buffer sized for a
  // directory this class has no business knowing the size of. So: walk to the
  // first file, stop the walk, delete it, walk again. Config directories hold
  // a handful of files, which makes the extra passes free.
  while (true) {
    char victim[HCONFIG_PATH_BUFFER_SIZE] = "";
    bool sawFile = false;
    bool namedIt = false;

    auto takeFirst = [&victim, &sawFile, &namedIt](const char* name, bool isDirectory) -> bool {
      if (name == nullptr || isDirectory) {
        return true;  // Keep walking. Nothing this class writes is a directory.
      }

      sawFile = true;
      const int written = snprintf(victim, sizeof(victim), "config/%s", name);
      namedIt = written > 0 && static_cast<size_t>(written) < sizeof(victim);
      return false;  // Stop: whatever follows belongs to the next pass.
    };

    if (!HFs::HFileSystem::listDir("config", HFsEntryVisitor::create(takeFirst))) {
      HCritical("config/ could not be listed - %u file(s) removed before that",
                static_cast<unsigned>(removed));
      return false;
    }

    if (!sawFile) {
      break;  // Empty. Either it always was, or this loop just made it so.
    }

    // A name too long to build a path from would be found again on the next
    // pass and never deleted, which is an infinite loop rather than an error.
    // Refusing is the only safe answer.
    if (!namedIt) {
      HCritical("a file in config/ has a name too long for a %u-byte path - stopping",
                static_cast<unsigned>(sizeof(victim)));
      return false;
    }

    if (!HFs::HFileSystem::deleteFile(victim)) {
      HCritical("%s could not be deleted - stopping after %u", victim,
                static_cast<unsigned>(removed));
      return false;
    }

    ++removed;
  }

  HInfo("configuration cleared - %u file(s) removed", static_cast<unsigned>(removed));
  return true;
}

// The staging shims (getConfig / setConfig / save / saveAll / resetCache) stood
// here and are gone. Every caller now speaks the real API: a batch of settings
// goes out in one write(), a single field through patch(). Nothing is left that
// wants a cache, and nothing calls a function whose name promises staging that
// no longer happens.
