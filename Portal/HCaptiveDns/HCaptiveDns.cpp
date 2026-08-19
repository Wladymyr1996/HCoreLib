#include "HCaptiveDns/HCaptiveDns.hpp"

#define HLOG_MODULE_NAME "Dns"
#include <HLog/HLog.hpp>

#include <cstring>

#include <lwip/sockets.h>

#include <HSystemUtils/HSystemUtils.hpp>

#include <HCoreLib.h>
#include "HNetwork/HNetwork.hpp"

namespace {

/** @brief A DNS message over UDP is capped at 512 bytes without EDNS. */
constexpr size_t kBufferSize = 512;

/** @brief Bytes in a DNS header: id, flags, and four section counts. */
constexpr size_t kHeaderSize = 12;

/** @brief Seconds a client may cache the answer. Short: the device is not a nameserver. */
constexpr uint32_t kAnswerTtl = 60;

constexpr uint16_t kTypeA = 1;
constexpr uint16_t kClassIn = 1;

uint16_t readBe16(const uint8_t* data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

void writeBe16(uint8_t* data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value >> 8);
  data[1] = static_cast<uint8_t>(value);
}

/**
 * @brief Walks a question's name to the byte after it.
 *
 * Labels are length-prefixed and end at a zero byte. Compression pointers
 * (top two bits set) are rejected rather than followed: they cannot legally
 * appear in a question, and following one from an untrusted packet is how a
 * parser like this ends up in a loop.
 *
 * @return Offset of the byte after the name, or 0 if the name is malformed.
 */
size_t skipName(const uint8_t* packet, size_t length, size_t offset) {
  while (offset < length) {
    const uint8_t label = packet[offset];

    if (label == 0) {
      return offset + 1;
    }

    if ((label & 0xC0u) != 0) {
      return 0;
    }

    offset += static_cast<size_t>(label) + 1;
  }

  return 0;
}

/**
 * @brief Turns a query into an answer in place.
 * @return The reply's length, or 0 if this packet deserves no reply.
 */
size_t buildReply(uint8_t* packet, size_t length, uint32_t address) {
  if (length < kHeaderSize) {
    return 0;
  }

  const bool isResponse = (packet[2] & 0x80u) != 0;
  const uint8_t opcode = static_cast<uint8_t>((packet[2] >> 3) & 0x0Fu);
  const uint16_t questions = readBe16(&packet[4]);

  // Only plain queries carrying exactly one question. Anything else is either
  // not for us or not worth the code to handle on a device like this.
  if (isResponse || opcode != 0 || questions != 1) {
    return 0;
  }

  const size_t nameEnd = skipName(packet, length, kHeaderSize);
  if (nameEnd == 0 || nameEnd + 4 > length) {
    return 0;
  }

  const uint16_t type = readBe16(&packet[nameEnd]);
  const uint16_t klass = readBe16(&packet[nameEnd + 2]);
  const size_t questionEnd = nameEnd + 4;

  // QR=1, authoritative, recursion available. Copying RD back is what stops a
  // resolver deciding the answer was unsolicited.
  packet[2] = static_cast<uint8_t>(0x80u | 0x04u | (packet[2] & 0x01u));
  packet[3] = 0x80u;

  writeBe16(&packet[6], 0);  // No answers, unless the loop below adds one.
  writeBe16(&packet[8], 0);  // No authority records.
  writeBe16(&packet[10], 0); // No additional records.

  // Anything that is not an IPv4 lookup gets an empty NOERROR: a phone asking
  // for AAAA has to be told "nothing here" quickly, because leaving it to time
  // out is a portal that takes ten seconds to appear.
  if (type != kTypeA || klass != kClassIn) {
    return questionEnd;
  }

  if (questionEnd + 16 > kBufferSize) {
    return questionEnd;
  }

  uint8_t* answer = &packet[questionEnd];

  // 0xC00C: a pointer back to the name at offset 12, rather than repeating it.
  answer[0] = 0xC0u;
  answer[1] = 0x0Cu;
  writeBe16(&answer[2], kTypeA);
  writeBe16(&answer[4], kClassIn);
  answer[6] = static_cast<uint8_t>(kAnswerTtl >> 24);
  answer[7] = static_cast<uint8_t>(kAnswerTtl >> 16);
  answer[8] = static_cast<uint8_t>(kAnswerTtl >> 8);
  answer[9] = static_cast<uint8_t>(kAnswerTtl);
  writeBe16(&answer[10], 4);

  // The address is already in network order, which is the order it goes out in.
  std::memcpy(&answer[12], &address, 4);

  writeBe16(&packet[6], 1);
  return questionEnd + 16;
}

}  // namespace

HCaptiveDns::HCaptiveDns() noexcept : HTask("dns", HCAPTIVEDNS_TASK_STACK, HCAPTIVEDNS_TASK_PRIORITY) {
}

void HCaptiveDns::run() {
  const int socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (socketHandle < 0) {
    HCritical("could not open a socket - no captive portal on this run");
    return;
  }

  sockaddr_in local = {};
  local.sin_family = AF_INET;
  local.sin_port = htons(53);
  local.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(socketHandle, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
    HCritical("port 53 is taken - no captive portal on this run");
    close(socketHandle);
    return;
  }

  HInfo("answering every name with %s", HNetwork::ip());

  uint8_t buffer[kBufferSize];

  for (;;) {
    sockaddr_in from = {};
    socklen_t fromLength = sizeof(from);

    const int received = recvfrom(socketHandle, buffer, sizeof(buffer), 0,
                                  reinterpret_cast<sockaddr*>(&from), &fromLength);
    if (received <= 0) {
      // The interface going down closes the socket under us. Nothing here can
      // fix that, and spinning on the error would burn the CPU until the reboot.
      HWarning("receive failed - stopping");
      break;
    }

    // Read fresh each time rather than cached at start-up: the answer has to be
    // whatever address the interface holds now.
    const size_t length = buildReply(buffer, static_cast<size_t>(received), HNetwork::ipv4());
    if (length == 0) {
      continue;
    }

    sendto(socketHandle, buffer, length, 0, reinterpret_cast<sockaddr*>(&from), fromLength);
  }

  close(socketHandle);
}
