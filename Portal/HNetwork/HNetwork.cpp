#include "HNetwork/HNetwork.hpp"

#define HLOG_MODULE_NAME "Net"
#include <HLog/HLog.hpp>

#include <cstdio>
#include <cstring>

#include <esp_event.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <mdns.h>
#include <nvs_flash.h>

#include <HWebServer/HWebServer.hpp>

namespace {

/** @brief Longest SSID the standard allows, terminator included. */
constexpr size_t kSsidBufferSize = 33;

/** @brief Enough for "255.255.255.255". */
constexpr size_t kIpBufferSize = 16;

char ssidBuffer[kSsidBufferSize] = "";
char ipBuffer[kIpBufferSize] = "-";

/** @brief The same address the buffer above spells out, in network byte order. */
uint32_t ipRaw = 0;

HNetworkStatus currentStatus = HNetworkStatus::Down;
uint32_t versionCounter = 0;

/** @brief Records a state change and asks the screen to redraw. */
void setStatus(HNetworkStatus status) noexcept {
  if (currentStatus == status) {
    return;
  }
  currentStatus = status;
  ++versionCounter;
}

/** @brief Stores an address as text and asks the screen to redraw. */
void setIp(const esp_ip4_addr_t& address) noexcept {
  snprintf(ipBuffer, sizeof(ipBuffer), IPSTR, IP2STR(&address));
  ipRaw = address.addr;
  ++versionCounter;
}

/**
 * @brief Starts mDNS, so the device answers to a NAME as well as an address.
 *
 * `hatynka.local` works on iOS and macOS out of the box, on Windows 10 and
 * later, and on recent Android - and it works in station mode too, where the
 * captive DNS below is deliberately not running. It is the only way to reach
 * this device by name on somebody else's network.
 *
 * A note on names: a real TLD would be a mistake here. The whole `.app` domain
 * is in the browsers' HSTS preload list, so `http://anything.app` is rewritten
 * to `https://` before the request leaves the phone - and this device has no
 * certificate. `.local` is reserved for exactly this.
 */
void startMdns() noexcept {
  if (mdns_init() != ESP_OK) {
    HWarning("mDNS did not start - the device is reachable by address only");
    return;
  }

  mdns_hostname_set(HNETWORK_MDNS_HOSTNAME);
  mdns_instance_name_set(HNETWORK_MDNS_INSTANCE);

  // Advertising the web server is what puts the device in a phone's list of
  // discovered services, and costs one record.
  mdns_service_add(nullptr, "_http", "_tcp", HWEBSERVER_PORT, nullptr, 0);

  HInfo("also reachable at http://%s.local/", HNETWORK_MDNS_HOSTNAME);
}

#if HNETWORK_MODE_AP

/**
 * @brief Builds the access point's name from this device's own MAC.
 *
 * Three bytes is 16 million names - enough that two devices in one room will
 * not collide - and reading them from the chip means the name is a property of
 * the hardware rather than something stored, so it survives a factory reset and
 * cannot drift from what is printed on a label.
 */
void buildApSsid() noexcept {
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) {
    snprintf(ssidBuffer, sizeof(ssidBuffer), HNETWORK_AP_SSID_PREFIX "000000");
    HWarning("could not read the MAC - the access point is named %s", ssidBuffer);
    return;
  }

  snprintf(ssidBuffer, sizeof(ssidBuffer), HNETWORK_AP_SSID_PREFIX "%02X%02X%02X", mac[3], mac[4],
           mac[5]);
}

#endif  // HNETWORK_MODE_AP

/** @brief Wi-Fi and IP events, both of which only matter in station mode. */
void onEvent(void* handlerArg, esp_event_base_t base, int32_t id, void* data) {
  (void)handlerArg;

  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    setStatus(HNetworkStatus::Connecting);
    esp_wifi_connect();
    return;
  }

  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    // Retried immediately and forever: this mode exists to be reachable, and a
    // portal that gave up after N attempts would be a device somebody has to
    // walk over to and reset.
    std::snprintf(ipBuffer, sizeof(ipBuffer), "-");
    setStatus(HNetworkStatus::Connecting);
    esp_wifi_connect();
    return;
  }

  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const ip_event_got_ip_t* event = static_cast<const ip_event_got_ip_t*>(data);
    setIp(event->ip_info.ip);
    setStatus(HNetworkStatus::Connected);
    startMdns();
    HInfo("joined '%s' as %s", ssidBuffer, ipBuffer);
    return;
  }
}

/** @brief NVS, the interface layer and the event loop - everything Wi-Fi needs under it. */
bool startPlatform() noexcept {
  esp_err_t result = nvs_flash_init();
  if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // A partition left over from another build, or one that filled up. Wi-Fi
    // keeps calibration data here, so it must be usable before the radio starts.
    HWarning("NVS needs erasing - doing it now");
    ESP_ERROR_CHECK(nvs_flash_erase());
    result = nvs_flash_init();
  }

  if (result != ESP_OK) {
    HCritical("NVS init failed: %s", esp_err_to_name(result));
    return false;
  }

  if (esp_netif_init() != ESP_OK) {
    HCritical("netif init failed");
    return false;
  }

  result = esp_event_loop_create_default();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    HCritical("event loop failed: %s", esp_err_to_name(result));
    return false;
  }

  return true;
}

#if HNETWORK_MODE_AP

/** @brief Comes up as an open access point serving the portal. */
bool startAccessPoint() noexcept {
  esp_netif_t* netif = esp_netif_create_default_wifi_ap();
  if (netif == nullptr) {
    HCritical("could not create the access point interface");
    return false;
  }

  buildApSsid();

  // RFC 8910: the DHCP lease itself can name the portal, which a modern phone
  // reads and acts on WITHOUT needing its probe hijacked. It has to be set
  // before the DHCP server starts, which happens with the interface.
  static const char kPortalUri[] = "http://" HNETWORK_AP_IP "/";
  const esp_err_t portalUri =
      esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI,
                             const_cast<char*>(kPortalUri), sizeof(kPortalUri) - 1);
  if (portalUri != ESP_OK) {
    // Not fatal: the DNS answer and the redirect below still bring the portal
    // up on phones that ignore or never see this option.
    HDebug("captive portal DHCP option not accepted: %s", esp_err_to_name(portalUri));
  }

  wifi_config_t config = {};
  std::strncpy(reinterpret_cast<char*>(config.ap.ssid), ssidBuffer, sizeof(config.ap.ssid));
  config.ap.ssid_len = static_cast<uint8_t>(std::strlen(ssidBuffer));
  config.ap.channel = 1;
  config.ap.max_connection = HNETWORK_AP_MAX_CLIENTS;
  config.ap.authmode = WIFI_AUTH_OPEN;

  if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK ||
      esp_wifi_set_config(WIFI_IF_AP, &config) != ESP_OK || esp_wifi_start() != ESP_OK) {
    HCritical("access point failed to start");
    return false;
  }

  // The address of an ESP32 access point is fixed by its DHCP server, so it is
  // known the moment the interface exists - no event to wait for.
  esp_netif_ip_info_t info = {};
  if (esp_netif_get_ip_info(netif, &info) == ESP_OK) {
    setIp(info.ip);
  }

  setStatus(HNetworkStatus::Connected);
  startMdns();

  HInfo("access point '%s' is up at %s (open)", ssidBuffer, ipBuffer);
  return true;
}

#endif  // HNETWORK_MODE_AP

#if !HNETWORK_MODE_AP

/** @brief Joins the router named in HCoreLibConfig.h. Debug builds only. */
bool startStation() noexcept {
  if (esp_netif_create_default_wifi_sta() == nullptr) {
    HCritical("could not create the station interface");
    return false;
  }

  snprintf(ssidBuffer, sizeof(ssidBuffer), HNETWORK_STA_SSID);

  wifi_config_t config = {};
  std::strncpy(reinterpret_cast<char*>(config.sta.ssid), HNETWORK_STA_SSID,
               sizeof(config.sta.ssid));
  std::strncpy(reinterpret_cast<char*>(config.sta.password), HNETWORK_STA_PASS,
               sizeof(config.sta.password));

  if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
      esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK || esp_wifi_start() != ESP_OK) {
    HCritical("station failed to start");
    return false;
  }

  HInfo("joining '%s'...", ssidBuffer);
  return true;
}

#endif  // !HNETWORK_MODE_AP

}  // namespace

bool HNetwork::start() noexcept {
  if (!startPlatform()) {
    return false;
  }

  const wifi_init_config_t initConfig = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&initConfig) != ESP_OK) {
    HCritical("wifi init failed");
    return false;
  }

  // Registered before the radio starts, so the first STA_START is not missed.
  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onEvent, nullptr, nullptr);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &onEvent, nullptr, nullptr);

  // The radio is only ever on in Configuring mode, where the device is mains-
  // adjacent and being looked at, so the power-save mode that would delay a
  // response by up to a beacon interval is not worth its microamps here.
  esp_wifi_set_ps(WIFI_PS_NONE);

#if HNETWORK_MODE_AP
  return startAccessPoint();
#else
  return startStation();
#endif
}

HNetworkMode HNetwork::mode() noexcept {
#if HNETWORK_MODE_AP
  return HNetworkMode::AccessPoint;
#else
  return HNetworkMode::Station;
#endif
}

HNetworkStatus HNetwork::status() noexcept {
  return currentStatus;
}

const char* HNetwork::statusText() noexcept {
  switch (currentStatus) {
    case HNetworkStatus::Connected:
      return "Connected";
    case HNetworkStatus::Connecting:
      return "Connecting";
    default:
      return "Disconnected";
  }
}

const char* HNetwork::ssid() noexcept {
  return ssidBuffer;
}

const char* HNetwork::ip() noexcept {
  return ipBuffer;
}

uint32_t HNetwork::ipv4() noexcept {
  return ipRaw;
}

const char* HNetwork::hostname() noexcept {
  return HNETWORK_MDNS_HOSTNAME ".local";
}

uint32_t HNetwork::version() noexcept {
  return versionCounter;
}
