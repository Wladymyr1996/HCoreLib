#pragma once

#include <cstddef>
#include <cstdint>

#include <HCoreLib.h>
#include <etl/delegate.h>

/** How much of the download is read at a time, in bytes. */
#ifndef HOTAPULL_CHUNK_SIZE
#define HOTAPULL_CHUNK_SIZE 4096
#endif

/**
 * How long the whole attempt may take, in ms - joining, downloading and all.
 *
 * The most important number in this module. A node that cannot reach the router
 * must cost ONE attempt, never a battery: with the radio on, this device draws
 * tens of milliamps, and a retry loop with no ceiling on a node up a pole is the
 * difference between a missed update and a dead device.
 *
 * Three minutes is generous for a 1.2 MB image over a domestic router and short
 * enough that a silent server cannot hold the radio on into a second reporting
 * period.
 */
#ifndef HOTAPULL_TIMEOUT_MS
#define HOTAPULL_TIMEOUT_MS 180000
#endif

/** How long to wait for the router to hand over an address, in ms. */
#ifndef HOTAPULL_JOIN_TIMEOUT_MS
#define HOTAPULL_JOIN_TIMEOUT_MS 30000
#endif

/** How long one socket read may stall before the attempt is given up, in ms. */
#ifndef HOTAPULL_HTTP_TIMEOUT_MS
#define HOTAPULL_HTTP_TIMEOUT_MS 10000
#endif

/** @brief Why a pull ended. */
enum class HOtaPullResult : uint8_t {
  /** Written, verified and marked bootable. Restart to run it. */
  Installed,

  /** The router did not accept this node, or never handed over an address. */
  NoNetwork,

  /** The server did not answer, answered with an error, or stopped mid-image. */
  NoImage,

  /** The image arrived and was refused - see HOtaWriter::reason(). */
  Refused,

  /** The whole attempt ran past HOTAPULL_TIMEOUT_MS. */
  TimedOut
};

/**
 * @brief Fetching a firmware image over plain HTTP, in Ota mode.
 *
 * The other half of OTA: HRestApi takes an image a browser pushes, and this
 * goes and gets one. Both hand bytes to HOtaWriter, which is where every
 * decision about whether an image may be written actually lives - this module
 * owns the network and nothing else.
 *
 * ## It runs on a task with a watchdog
 * perform() blocks for as long as a download takes, which is far longer than
 * any watchdog allows, so it calls the progress hook between chunks and expects
 * the caller to report in from there. That is why the hook exists at all - it
 * is not only for the screen.
 *
 * @code
 *   HOtaPull::onProgress(HOtaProgressHook::create<&MainTask::otaTick>(*this));
 *   const HOtaPullResult result = HOtaPull::perform(request.ssid, ...);
 * @endcode
 *
 * ## What it does not do
 * It does not reboot, and it does not decide what a failure means. A caller
 * restarts into Normal on anything but Installed, which leaves the node on the
 * firmware it already had, with its stored bind untouched - so it rejoins its
 * parent with nobody involved.
 *
 * ## No TLS
 * The URL is plain HTTP. The image's own SHA-256 is checked by HOtaWriter, which
 * catches a truncated or corrupted download - it does NOT catch a substituted
 * one, because that hash travels inside the file rather than being a secret.
 * What authenticates an update here is the INSTRUCTION: the URL arrived over an
 * encrypted ESP-NOW link from a bound parent. The bytes are trusted because the
 * network they come from is. See Docs/Ota.md.
 */
using HOtaProgressHook = etl::delegate<void(size_t received, size_t total)>;

class HOtaPull {
 public:
  HOtaPull() = delete;

  /**
   * @brief Called between chunks. Where a caller feeds its watchdog and draws.
   *
   * @param received Bytes taken so far.
   * @param total Bytes the server promised, or 0 when it did not say.
   */
  static void onProgress(HOtaProgressHook hook) noexcept;

  /**
   * @brief Joins the network, downloads the image and writes it. Blocks.
   *
   * @param ssid Router to join.
   * @param passphrase Its passphrase; empty for an open network.
   * @param channel The channel it is on, or 0 to scan.
   * @param url Where the image is - `http://host[:port]/path`.
   * @param allowSameVersion Whether reinstalling the running version is allowed.
   * @return What happened. Only Installed means a restart runs new firmware.
   */
  static HOtaPullResult perform(const char* ssid, const char* passphrase, uint8_t channel,
                                const char* url, bool allowSameVersion) noexcept;

  /** @brief A short reason, for a log line and the screen. Never null. */
  static const char* resultText(HOtaPullResult result) noexcept;
};
