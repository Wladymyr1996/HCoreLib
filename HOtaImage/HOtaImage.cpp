#include "HOtaImage/HOtaImage.hpp"

#include <cstring>

namespace {

/** Offset of chip_id inside esp_image_header_t, which is packed and little-endian. */
constexpr size_t kChipIdOffset = 12;

/** Offsets inside esp_app_desc_t, relative to HOTAIMAGE_DESC_OFFSET. */
constexpr size_t kDescMagicOffset = 0;
constexpr size_t kDescVersionOffset = 16;   // magic_word, secure_version, reserv1[2]
constexpr size_t kDescProjectOffset = 48;   // version[32] behind it
constexpr size_t kDescIdfVersionOffset = 112;  // time[16] and date[16] behind that

/** @brief Reads a little-endian u16. Both ends of this are little-endian, but saying so costs nothing. */
uint16_t readU16(const uint8_t* data) noexcept {
  return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) |
                               (static_cast<uint16_t>(data[1]) << 8));
}

/** @brief Reads a little-endian u32. */
uint32_t readU32(const uint8_t* data) noexcept {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

/**
 * @brief Copies one of the descriptor's fixed fields out as a C string.
 *
 * The descriptor's fields are fixed-width and are NOT required to carry a
 * terminator: a project name of exactly 32 characters fills its field edge to
 * edge. Copying HOTAIMAGE_STRING_LEN bytes and terminating afterwards is what
 * keeps a full field from running into the next one.
 */
void copyField(const uint8_t* source, char* destination) noexcept {
  std::memcpy(destination, source, HOTAIMAGE_STRING_LEN);
  destination[HOTAIMAGE_STRING_LEN] = '\0';
}

}  // namespace

HOtaVerdict HOtaImage::parse(const uint8_t* data, size_t size, HOtaImageInfo& out) noexcept {
  if (data == nullptr || size < HOTAIMAGE_HEADER_BYTES) {
    return HOtaVerdict::Incomplete;
  }

  if (data[0] != HOTAIMAGE_MAGIC) {
    return HOtaVerdict::NotAnImage;
  }

  const uint8_t* descriptor = data + HOTAIMAGE_DESC_OFFSET;
  if (readU32(descriptor + kDescMagicOffset) != HOTAIMAGE_DESC_MAGIC) {
    return HOtaVerdict::NoDescriptor;
  }

  out.chipId = readU16(data + kChipIdOffset);
  copyField(descriptor + kDescVersionOffset, out.version);
  copyField(descriptor + kDescProjectOffset, out.projectName);
  copyField(descriptor + kDescIdfVersionOffset, out.idfVersion);

  return HOtaVerdict::Ok;
}

HOtaVerdict HOtaImage::judge(const HOtaImageInfo& info, size_t imageSize,
                             const HOtaPolicy& policy) noexcept {
  // The chip comes first because it is the failure that cannot be recovered
  // from over the air: an image for another target passes every string check
  // and then does not boot, and the device is on a pole.
  if (info.chipId != policy.expectedChipId) {
    return HOtaVerdict::WrongChip;
  }

  if (policy.expectedProject == nullptr ||
      std::strcmp(info.projectName, policy.expectedProject) != 0) {
    return HOtaVerdict::WrongProject;
  }

  // 0 means the total is not known - a chunked upload with no Content-Length.
  // Skipped rather than guessed at: esp_ota_write() refuses honestly if the
  // image overruns the slot, and a guess here would refuse good images.
  if (policy.partitionSize != 0 && imageSize != 0 && imageSize > policy.partitionSize) {
    return HOtaVerdict::TooLarge;
  }

  int order = 0;
  if (!compareVersions(info.version, policy.runningVersion, order)) {
    // Either side unparseable. Refusing is the only safe answer: a version that
    // cannot be ordered cannot be shown to be an upgrade, and this check is the
    // whole of what stops a downgrade.
    return HOtaVerdict::BadVersion;
  }

  if (order < 0 && !policy.allowDowngrade) {
    return HOtaVerdict::OlderVersion;
  }

  if (order == 0 && !policy.allowSameVersion) {
    return HOtaVerdict::SameVersion;
  }

  return HOtaVerdict::Ok;
}

HOtaVerdict HOtaImage::inspect(const uint8_t* data, size_t size, size_t imageSize,
                               const HOtaPolicy& policy, HOtaImageInfo& out) noexcept {
  const HOtaVerdict parsed = parse(data, size, out);
  if (parsed != HOtaVerdict::Ok) {
    return parsed;
  }

  return judge(out, imageSize, policy);
}

const char* HOtaImage::verdictText(HOtaVerdict verdict) noexcept {
  switch (verdict) {
    case HOtaVerdict::Ok:
      return "ok";
    case HOtaVerdict::Incomplete:
      return "incomplete";
    case HOtaVerdict::NotAnImage:
      return "not an image";
    case HOtaVerdict::WrongChip:
      return "wrong chip";
    case HOtaVerdict::NoDescriptor:
      return "no descriptor";
    case HOtaVerdict::WrongProject:
      return "wrong device";
    case HOtaVerdict::BadVersion:
      return "bad version";
    case HOtaVerdict::OlderVersion:
      return "older version";
    case HOtaVerdict::SameVersion:
      return "same version";
    case HOtaVerdict::TooLarge:
      return "too large";
    case HOtaVerdict::WriteFailed:
      return "write failed";
    case HOtaVerdict::VerifyFailed:
      return "verify failed";
  }

  return "unknown";
}

bool HOtaImage::parseVersion(const char* text, uint32_t& major, uint32_t& minor,
                             uint32_t& patch) noexcept {
  if (text == nullptr || *text == '\0') {
    return false;
  }

  uint32_t components[3] = {0, 0, 0};
  const char* cursor = text;

  for (size_t index = 0; index < 3; ++index) {
    // A component must START with a digit. Without this, "v1.2.3" would parse
    // as 0.0.0 and compare equal to every other unnumbered string.
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }

    uint32_t value = 0;
    while (*cursor >= '0' && *cursor <= '9') {
      // Saturate rather than wrap. A version number long enough to overflow is
      // nonsense either way, and wrapping would make it compare SMALL - which
      // is the one outcome that could let a downgrade through.
      if (value <= (0xFFFFFFFFUL - 9) / 10) {
        value = value * 10 + static_cast<uint32_t>(*cursor - '0');
      }
      ++cursor;
    }

    components[index] = value;

    // A pre-release suffix or build metadata ends the numeric part. Everything
    // after it is ignored for ordering - see the header for why.
    if (*cursor == '-' || *cursor == '+' || *cursor == '\0') {
      break;
    }

    if (*cursor != '.') {
      return false;
    }
    ++cursor;
  }

  major = components[0];
  minor = components[1];
  patch = components[2];
  return true;
}

bool HOtaImage::compareVersions(const char* a, const char* b, int& outResult) noexcept {
  uint32_t aMajor = 0, aMinor = 0, aPatch = 0;
  uint32_t bMajor = 0, bMinor = 0, bPatch = 0;

  if (!parseVersion(a, aMajor, aMinor, aPatch) || !parseVersion(b, bMajor, bMinor, bPatch)) {
    return false;
  }

  if (aMajor != bMajor) {
    outResult = (aMajor < bMajor) ? -1 : 1;
    return true;
  }
  if (aMinor != bMinor) {
    outResult = (aMinor < bMinor) ? -1 : 1;
    return true;
  }
  if (aPatch != bPatch) {
    outResult = (aPatch < bPatch) ? -1 : 1;
    return true;
  }

  outResult = 0;
  return true;
}
