#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @file HOtaImage.hpp
 * @brief Deciding whether an offered firmware image may be written at all.
 *
 * The verdict enum and the two plain structures share this header with the
 * class: none of them has behaviour, so there is nothing for a .cpp of their
 * own to hold and no vtable to anchor.
 *
 * ## Why this parses the layout itself instead of including esp_app_format.h
 * Nothing about reading a header and comparing two version strings needs a
 * radio, a flash driver or an IDF. Keeping this free of them is what lets the
 * code that decides to overwrite a device's only working firmware be driven by
 * a host test, on a machine with no hardware attached, against bytes a test
 * wrote by hand - the same argument that puts HAuth in HCoreLib proper rather
 * than in the portal component.
 *
 * The layout below is the on-flash application image format, which is a
 * published, versioned contract rather than an internal detail. It is checked
 * against the real thing in HOtaImageTest.cpp, which builds a header from these
 * offsets and asserts the constants match ESP-IDF's own.
 */

/**
 * Bytes that must be in hand before a verdict is possible: the 24-byte image
 * header, the 8-byte first segment header, and the 256-byte application
 * descriptor behind them.
 *
 * A caller streaming an upload must therefore buffer at least this much before
 * committing anything to flash. That is the whole point - a chunk size smaller
 * than this would mean writing bytes of an image nobody has judged yet.
 */
#define HOTAIMAGE_HEADER_BYTES 288

/** Offset of the application descriptor: sizeof(image header) + sizeof(segment header). */
#define HOTAIMAGE_DESC_OFFSET 32

/** Capacity of the descriptor's own fixed strings, terminator excluded. */
#define HOTAIMAGE_STRING_LEN 32

/** ESP-IDF's ESP_IMAGE_HEADER_MAGIC - byte 0 of every application image. */
#define HOTAIMAGE_MAGIC 0xE9

/** ESP-IDF's ESP_APP_DESC_MAGIC_WORD, at HOTAIMAGE_DESC_OFFSET. */
#define HOTAIMAGE_DESC_MAGIC 0xABCD5432UL

/** ESP-IDF's ESP_CHIP_ID_ESP32C6. The only chip this firmware runs on. */
#define HOTAIMAGE_CHIP_ESP32C6 0x000D

/**
 * @brief Why an image was accepted or turned away.
 *
 * Ordered so that the cheapest and most fundamental failures come first: an
 * answer of WrongChip means nothing was read past byte 13, and a caller can
 * refuse an upload before it has spent a second receiving it.
 */
enum class HOtaVerdict : uint8_t {
  /** Every check passed. The image may be written. */
  Ok,

  /** Fewer than HOTAIMAGE_HEADER_BYTES were offered - no verdict is possible yet. */
  Incomplete,

  /** Byte 0 is not 0xE9. Whatever this is, it is not an application image. */
  NotAnImage,

  /**
   * Built for a different chip. This is the failure that would otherwise brick
   * a device: every other check can pass on an image the bootloader cannot run.
   */
  WrongChip,

  /** No application descriptor. An image, but not one IDF built. */
  NoDescriptor,

  /**
   * Another product's firmware. Every Hatynka device shares this bootloader,
   * this chip and this descriptor format, so nothing above catches a door
   * controller's image arriving at a thermometer.
   */
  WrongProject,

  /** The version string is not major.minor.patch and cannot be ordered. */
  BadVersion,

  /** Older than what is running, and the policy did not allow a downgrade. */
  OlderVersion,

  /** Same as what is running, and the policy did not allow a reinstall. */
  SameVersion,

  /** Larger than the partition it would be written to. */
  TooLarge,

  /**
   * The flash write itself failed. Not a judgement on the image - the slot is
   * left unbootable, and the running firmware is untouched.
   */
  WriteFailed,

  /**
   * The image was written but did not match the SHA-256 its build appended.
   *
   * What a truncated upload or a connection that died halfway looks like. It is
   * the last check before anything is marked bootable, and the reason a partial
   * image can never be booted into.
   */
  VerifyFailed
};

/**
 * @brief What an image says about itself. Facts only - no policy applied.
 *
 * Every string is null-terminated here even though the descriptor's own fields
 * are not required to be: a fixed 32-byte field filled edge to edge has no
 * terminator, and handing that to anything taking a `const char*` reads off the
 * end of the structure.
 */
struct HOtaImageInfo {
  char projectName[HOTAIMAGE_STRING_LEN + 1];
  char version[HOTAIMAGE_STRING_LEN + 1];
  char idfVersion[HOTAIMAGE_STRING_LEN + 1];

  /** From the image header, not the descriptor. ESP_CHIP_ID_*. */
  uint16_t chipId;
};

/**
 * @brief What this particular device is willing to accept.
 *
 * Split from the image because the two come from different places and change
 * for different reasons: the info is read off the wire, and this is the
 * device's own policy, which an application configures.
 */
struct HOtaPolicy {
  /** The descriptor's project_name a legitimate image must carry. Never null. */
  const char* expectedProject;

  /** What is running now, from esp_app_get_description(). Never null. */
  const char* runningVersion;

  /** ESP_CHIP_ID_* this firmware runs on. */
  uint16_t expectedChipId;

  /** Bytes in the target OTA slot. 0 skips the size check. */
  size_t partitionSize;

  /**
   * Whether re-flashing the version already running is allowed.
   *
   * Normally yes: a device that refuses to reinstall its own version is a
   * device that cannot be repaired in the field after a partial write, and the
   * cost of allowing it is one unnecessary update nobody asked twice for.
   */
  bool allowSameVersion;

  /**
   * Whether an older version may replace a newer one.
   *
   * The asymmetry between the two OTA paths lives here rather than in either
   * of them: somebody standing in front of the device with the admin password
   * can deliberately roll back and watch what happens, and a downgrade pushed
   * over the air lands on a node nobody is looking at.
   */
  bool allowDowngrade;
};

/**
 * @brief Reads an image's header and judges it. A static facade.
 *
 * Two halves that are deliberately separate: parse() states what the image
 * says, judge() decides what to do about it. A caller that only wants to
 * display an offered version - the web UI does - needs the first without the
 * second, and a test can drive the policy over hand-written facts without
 * assembling 288 bytes for every case.
 *
 * @code
 *   HOtaImageInfo info;
 *   HOtaPolicy policy = {"HTermo", running->version, HOTAIMAGE_CHIP_ESP32C6,
 *                        partition->size, true, false};
 *
 *   const HOtaVerdict verdict = HOtaImage::inspect(buffer, received,
 *                                                 contentLength, policy, info);
 *   if (verdict != HOtaVerdict::Ok) {
 *     HWarning("refused %s: %s", info.version, HOtaImage::verdictText(verdict));
 *     return;
 *   }
 * @endcode
 */
class HOtaImage {
 public:
  HOtaImage() = delete;

  /**
   * @brief Fills `out` with what the image says about itself.
   *
   * Applies no policy at all: an image for the wrong chip parses perfectly
   * well, and saying so is more useful than refusing to look.
   *
   * @param data First bytes of the image, from offset 0.
   * @param size How many are in hand.
   * @param out Filled only when this returns Ok; untouched otherwise.
   * @return Ok, Incomplete, NotAnImage or NoDescriptor - the failures that stop
   *         the header being readable at all.
   */
  static HOtaVerdict parse(const uint8_t* data, size_t size, HOtaImageInfo& out) noexcept;

  /**
   * @brief Decides whether an image these facts describe may be written.
   *
   * @param imageSize Total bytes the image will occupy, or 0 when it is not
   *        known - a chunked upload with no Content-Length. The size check is
   *        skipped then rather than guessed at; esp_ota_write() fails honestly
   *        if it overruns the slot.
   */
  static HOtaVerdict judge(const HOtaImageInfo& info, size_t imageSize,
                           const HOtaPolicy& policy) noexcept;

  /** @brief parse() then judge(), which is what every real caller wants. */
  static HOtaVerdict inspect(const uint8_t* data, size_t size, size_t imageSize,
                             const HOtaPolicy& policy, HOtaImageInfo& out) noexcept;

  /**
   * @brief A short reason, for a log line and for the REST body. Never null.
   *
   * Deliberately the same string in both places: a person reading the device's
   * console and a person reading the browser's error should not have to
   * translate between two vocabularies for the same refusal.
   */
  static const char* verdictText(HOtaVerdict verdict) noexcept;

  /**
   * @brief Parses `major[.minor[.patch]]`, ignoring any `-suffix` or `+build`.
   *
   * A missing component is zero, so "1.2" orders as 1.2.0 - which is what
   * somebody who typed it meant. A pre-release suffix is ignored for ordering
   * rather than ranked: semver's rules there would have "1.0.0-rc1" install
   * over "1.0.0", and this refuses to be that clever about somebody's firmware.
   *
   * @return false when the string is empty, or the first component is not a
   *         number - both of which make the version unorderable.
   */
  static bool parseVersion(const char* text, uint32_t& major, uint32_t& minor,
                           uint32_t& patch) noexcept;

  /**
   * @brief Orders two version strings.
   * @param outResult -1 if `a` is older than `b`, 0 if equal, 1 if newer.
   * @return false when either string is unparseable, in which case outResult is
   *         untouched. An unorderable version must not silently compare equal.
   */
  static bool compareVersions(const char* a, const char* b, int& outResult) noexcept;
};
