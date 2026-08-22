#pragma once

#include <cstdint>

#include <HCoreLib.h>

/**
 * 1 to serve the portal from the device's own access point, 0 to join an
 * existing router instead. The station build is a development convenience: it
 * needs a laptop on the same network, but keeps the serial console and the
 * network up at once.
 */
#ifndef HNETWORK_MODE_AP
#define HNETWORK_MODE_AP 1
#endif

/** Router to join when HNETWORK_MODE_AP is 0. Ignored otherwise. */
#ifndef HNETWORK_STA_SSID
#define HNETWORK_STA_SSID ""
#endif

#ifndef HNETWORK_STA_PASS
#define HNETWORK_STA_PASS ""
#endif

/**
 * The access point's name is this plus six hex digits of the device's own MAC.
 * Derived rather than random, so the network is unique per device AND the same
 * one every time - a name that changed each session would have its owner
 * hunting for it.
 */
#ifndef HNETWORK_AP_SSID_PREFIX
#define HNETWORK_AP_SSID_PREFIX "Hatynka-"
#endif

/**
 * 1 leaves the access point OPEN. Acceptable only because it exists for minutes
 * at a time, at arm's length, and because every route that changes anything
 * needs the admin password.
 */
#ifndef HNETWORK_AP_OPEN
#define HNETWORK_AP_OPEN 1
#endif

/** Clients the access point will hold at once. */
#ifndef HNETWORK_AP_MAX_CLIENTS
#define HNETWORK_AP_MAX_CLIENTS 2
#endif

/** The access point's own address, fixed by its DHCP server. */
#ifndef HNETWORK_AP_IP
#define HNETWORK_AP_IP "192.168.4.1"
#endif

/**
 * The name the device answers to over mDNS, giving `<hostname>.local`.
 *
 * `.local` and nothing else. A real TLD would be a trap - the whole `.app`
 * domain is in the browsers' HSTS preload list, so `http://anything.app` is
 * rewritten to `https://` before the request ever leaves the phone, and these
 * devices have no certificate to meet it with. `.local` is reserved for exactly
 * this and works on iOS, macOS and Windows out of the box.
 */
#ifndef HNETWORK_MDNS_HOSTNAME
#define HNETWORK_MDNS_HOSTNAME "hatynka"
#endif

/** Shown in a phone's list of discovered services. */
#ifndef HNETWORK_MDNS_INSTANCE
#define HNETWORK_MDNS_INSTANCE "Hatynka device"
#endif

/**
 * @brief Which way the radio is being used. Fixed at build time by
 *        HCoreLibConfig.h.
 */
enum class HNetworkMode : uint8_t {
  /** The device is the network: its own access point, one hop, no router. */
  AccessPoint,

  /** The device joined somebody else's network. A debugging convenience. */
  Station,
};

/** @brief How far along the connection is. Only Station ever reports Connecting. */
enum class HNetworkStatus : uint8_t {
  Down,
  Connecting,
  Connected,
};

/**
 * @brief The radio, and the three facts the portal screen has to show.
 *
 * Only Configuring mode ever calls start(). Normal and the measurement wake
 * never bring Wi-Fi up at all - which is the whole point of the boot-mode split:
 * a node doing its job has no radio in its working set, so it cannot be attacked
 * over one and does not spend the battery on it.
 *
 * ## Access point or station
 * One module for both, chosen by HNETWORK_MODE_AP, because the screen and
 * the web server want the same three answers either way: what network, in what
 * state, at what address.
 *
 * The access point's name is HNETWORK_AP_SSID_PREFIX plus six hex digits of the
 * device's MAC - unique per device and identical every session, so its owner
 * looks for one name rather than a new one each time. The network is OPEN;
 * see HNETWORK_AP_OPEN for what that is and is not worth.
 *
 * ## What it does not do
 * No sockets, no HTTP, no retries policy beyond reconnecting a dropped station.
 * WebServer sits on top of this and knows nothing about how the link came up.
 */
class HNetwork {
 public:
  HNetwork() = delete;

  /**
   * @brief Brings the radio up in the configured mode. Call once.
   *
   * Initialises NVS, the network interface layer and the default event loop
   * first - Wi-Fi needs all three, and nothing else in this firmware does, so
   * they belong here rather than in start-up.
   *
   * @return false if the radio could not be started. The portal is then
   *         unreachable, which the screen says rather than hiding.
   */
  static bool start() noexcept;

  /**
   * @brief Joins a named router, whatever HNETWORK_MODE_AP says. Call once.
   *
   * The runtime counterpart of start(): that one brings the radio up the way
   * this device was BUILT, and this brings it up the way it has just been
   * TOLD to. Ota mode is the caller - a node handed an SSID, a passphrase and a
   * URL over the mesh has to join a network nothing knew about at build time.
   *
   * The compile-time switch is left alone deliberately. It decides what the
   * portal is, which is a property of the product; this is one boot mode
   * borrowing the radio for one job, and the two must not be able to disagree.
   *
   * No captive DNS: this device is a CLIENT on somebody else's network here,
   * and answering every name on a router it does not own would be hijacking it.
   * mDNS still comes up with the address, from the shared event handler - one
   * record, for the seconds this mode lasts.
   *
   * @param ssid Network to join. Empty fails rather than joining anything.
   * @param passphrase WPA passphrase; empty for an open network.
   * @param channel The channel it is on, or 0 to scan. Naming it saves a
   *        battery node seconds of radio it does not have to spend.
   * @return false when the radio could not be started. Whether it CONNECTED is
   *         a later question - watch status().
   */
  static bool startStation(const char* ssid, const char* passphrase,
                           uint8_t channel) noexcept;

  /** @brief Stops the radio and frees it. For a mode that is done with it. */
  static void stop() noexcept;

  /** @brief Access point or station, as built. */
  static HNetworkMode mode() noexcept;

  /** @brief The link's current state. */
  static HNetworkStatus status() noexcept;

  /** @brief "Connecting", "Connected" or "Disconnected" - for the screen. */
  static const char* statusText() noexcept;

  /** @brief The access point's name, or the router being joined. Never null. */
  static const char* ssid() noexcept;

  /** @brief This device's address as text, or "-" before there is one. Never null. */
  static const char* ip() noexcept;

  /**
   * @brief The same address in network byte order, or 0 before there is one.
   *
   * For code that has to put an address into a packet rather than onto a screen -
   * see CaptiveDns.
   */
  static uint32_t ipv4() noexcept;

  /** @brief The name this device answers to on the local network, e.g. "hatynka.local". */
  static const char* hostname() noexcept;

  /**
   * @brief Bumped whenever the status or the address changes.
   *
   * The portal screen watches it the same way the value screens watch
   * DeviceState: a station that has just been given an address has to redraw,
   * and nothing else about the device moved.
   */
  static uint32_t version() noexcept;
};
