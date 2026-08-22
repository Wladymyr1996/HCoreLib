#pragma once

#include <cstddef>
#include <cstdint>

#include <HCoreLib.h>
#include <HOtaImage/HOtaImage.hpp>

/**
 * How much of the image is held back before anything is committed.
 *
 * It has to be at least HOTAIMAGE_HEADER_BYTES, because the verdict cannot be
 * reached without them - and holding back exactly that much is what lets a
 * refused image cost no flash at all. Bigger buys nothing: the bytes behind the
 * header are written as they arrive.
 */
#ifndef HOTAWRITER_STAGE_BYTES
#define HOTAWRITER_STAGE_BYTES HOTAIMAGE_HEADER_BYTES
#endif

/** @brief How far along a transfer is. */
enum class HOtaState : uint8_t {
  /** Nothing in progress. What a device looks like almost always. */
  Idle,

  /** begin() succeeded and bytes are being taken. */
  Writing,

  /** The image is written, verified and marked bootable. A restart runs it. */
  Done,

  /** Something was refused or failed. reason() says what. */
  Failed
};

/**
 * @brief Writing a firmware image into the inactive slot, and refusing the ones
 *        that must not be written.
 *
 * The half of OTA that both entry paths share: a browser pushing a file over the
 * device's own access point, and the device pulling one off a router in Ota
 * mode. Neither knows anything about the other, and both hand bytes to this.
 *
 * ## Nothing is written until the image has been judged
 * The first HOTAWRITER_STAGE_BYTES are held in a buffer rather than committed,
 * because that is the smallest amount that makes a verdict possible - see
 * HOtaImage. Only once the header passes is esp_ota_begin() called, and
 * esp_ota_begin() is what ERASES the slot. So an image for the wrong chip, for
 * another product, or older than what is running costs exactly nothing: the
 * inactive slot still holds whatever it held, and a device whose update was
 * refused is a device with its fallback intact.
 *
 * That ordering is the whole design. Erasing first and validating afterwards
 * would be simpler and would throw away the one copy of firmware that is known
 * to work.
 *
 * ## What it does not decide
 * When to reboot. finish() marks the new image bootable and stops; the restart
 * is the application's, a tick later, from the task that owns what the device is
 * doing. Restarting inside an HTTP handler drops the socket before the client
 * has read the answer, and the client then reports a failure for something that
 * worked - the same reason HFactoryReset only ever raises a flag.
 *
 * @code
 *   if (!HOtaWriter::begin(contentLength)) { ... }
 *
 *   while (more) {
 *     if (!HOtaWriter::write(chunk, chunkSize)) {
 *       // refused or failed; reason() says which, and the slot is untouched
 *       // if it was refused
 *       break;
 *     }
 *   }
 *
 *   if (HOtaWriter::finish()) {
 *     response.json("200 OK", body);
 *     HOtaWriter::requestReboot();   // MainTask acts on it a tick later
 *   }
 * @endcode
 *
 * ## One transfer at a time
 * A static facade, like HAuth and HFactoryReset, because there is one flash and
 * one inactive slot on a device. A second begin() while one is in flight is
 * refused rather than queued.
 */
class HOtaWriter {
 public:
  HOtaWriter() = delete;

  /**
   * @brief Starts a transfer. Erases nothing yet - see the class docs.
   *
   * @param expectedSize Total bytes the image will be, or 0 when that is not
   *        known. A known size lets the slot be erased only as far as it is
   *        needed, which is most of what makes a large update quick.
   * @param allowDowngrade Whether an older version may replace what is running.
   *        Per transfer rather than compiled in, because the two OTA paths
   *        answer it differently - see HOtaPolicy.
   * @param allowSameVersion Whether reinstalling the running version is allowed.
   *        Defaults to true, which is right for somebody standing in front of
   *        the device repairing it. The mesh path passes false unless the master
   *        asked for it, so a master cannot re-push the same image to a battery
   *        node every hour without meaning to.
   * @return false when a transfer is already in progress, or there is no
   *         inactive slot to write to.
   */
  static bool begin(size_t expectedSize, bool allowDowngrade,
                    bool allowSameVersion = true) noexcept;

  /**
   * @brief Takes the next bytes of the image.
   *
   * The first call that completes the header is where the verdict is reached
   * and where the slot is erased. Every call after that is a straight write.
   *
   * @return false the moment anything is refused or fails, after which the
   *         transfer is over and state() is Failed. A caller should stop
   *         reading and answer; calling again is harmless and returns false.
   */
  static bool write(const uint8_t* data, size_t size) noexcept;

  /**
   * @brief Verifies the image and marks it bootable.
   *
   * esp_ota_end() checks the SHA-256 the build appended, which is what catches
   * a truncated upload or a connection that died halfway. Nothing is marked
   * bootable until that passes.
   *
   * @return false when the image was incomplete, failed verification, or the
   *         boot partition could not be set. The running firmware is untouched
   *         in every one of those cases.
   */
  static bool finish() noexcept;

  /** @brief Gives up on a transfer in progress and frees the handle. */
  static void abort() noexcept;

  // -- what a UI and a log line ask ----------------------------------------

  static HOtaState state() noexcept;

  /** @brief Bytes committed so far, staging buffer included. */
  static size_t written() noexcept;

  /** @brief What begin() was told to expect, or 0 when it was not known. */
  static size_t total() noexcept;

  /** @brief Why the last transfer failed, or Ok. */
  static HOtaVerdict verdict() noexcept;

  /**
   * @brief A short reason, for a log line and for the REST body. Never null.
   *
   * The same string in both, deliberately: a person reading the console and a
   * person reading the browser should not have to translate between two
   * vocabularies for one refusal.
   */
  static const char* reason() noexcept;

  /**
   * @brief What the offered image said about itself.
   *
   * Filled in even when the image was REFUSED, which is the point: "you offered
   * 0.9.0 and this device runs 1.2.0" is an answer somebody can act on, and
   * "rejected" is not. Zeroed before the header has been seen.
   */
  static const HOtaImageInfo& offered() noexcept;

  // -- what is running now -------------------------------------------------

  /** @brief The running image's version, from its own descriptor. Never null. */
  static const char* runningVersion() noexcept;

  /** @brief The running image's project name. Never null. */
  static const char* runningProject() noexcept;

  /** @brief The slot an update would be written to, e.g. "ota_1". Never null. */
  static const char* slotName() noexcept;

  /** @brief That slot's size in bytes, or 0 when there is no inactive slot. */
  static size_t slotSize() noexcept;

  // -- the restart ---------------------------------------------------------

  /**
   * @brief Asks for a reboot at the next safe moment. Does NOT reboot.
   *
   * Read by the task that owns what the device is doing, the same way
   * HFactoryReset::isRequested() is.
   */
  static void requestReboot() noexcept;

  /** @brief True once requestReboot() has been called. */
  static bool isRebootRequested() noexcept;
};
