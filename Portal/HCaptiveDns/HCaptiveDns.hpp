#pragma once

#include <HCoreLib.h>
#include <HTask/HTask.hpp>

/** Stack for the DNS task, in bytes. It holds one 512-byte packet buffer. */
#ifndef HCAPTIVEDNS_TASK_STACK
#define HCAPTIVEDNS_TASK_STACK 3584
#endif

/** Priority. Below anything driving hardware; a name lookup can wait. */
#ifndef HCAPTIVEDNS_TASK_PRIORITY
#define HCAPTIVEDNS_TASK_PRIORITY 2
#endif

/**
 * @brief Answers every DNS question with this device's own address.
 *
 * The half of a captive portal that makes a phone notice it. A handset joining a
 * network immediately asks for a known name - `connectivitycheck.gstatic.com`,
 * `captive.apple.com` - and decides from the answer whether the network reaches
 * the internet. On an access point with no route anywhere, that lookup normally
 * fails and the phone quietly reports "no internet" instead of showing anything.
 *
 * Pointing every name at the device turns that probe into a request the web
 * server can answer, and answering it with a redirect is what raises the
 * "Sign in to network" notification. It is also what makes any name typed into
 * the address bar reach the portal.
 *
 * ## Only in access point mode
 * On a router's network this would be hijacking somebody else's DNS. The station
 * build never starts it.
 *
 * ## Its own task
 * recvfrom() blocks, which the shared update() tick forbids - see HITickable.
 */
class HCaptiveDns : public HTask {
 public:
  HCaptiveDns() noexcept;

 protected:
  /** @brief Serves UDP port 53 until the device reboots. */
  void run() override;
};
