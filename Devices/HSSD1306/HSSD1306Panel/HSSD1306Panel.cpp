#include "HSSD1306Panel.hpp"

#include <cstring>

#define HLOG_MODULE_NAME "HSSD1306"
#include <HLog/HLog.hpp>

namespace {

/** I2C control byte: everything after it is a command. */
const uint8_t kControlCommand = 0x00;

/**
 * @brief Power-on sequence, from the SSD1306xLED AVR driver in 3rdParty/SSD1315.
 *
 * Kept byte for byte with two changes, both marked: the addressing mode, and
 * the two values that depend on panel height (patched in configure() rather
 * than duplicated into a second table).
 */
const uint8_t kInitSequence[] = {
    0xAE,        // Display off while it is being configured
    0xD5, 0xF0,  // Clock divide / oscillator frequency
    0xA8, 0x3F,  // [1] Multiplex ratio - patched to height - 1
    0xD3, 0x00,  // Display offset: none
    0x40,        // Start line: row 0
    0x8D, 0x14,  // Charge pump on - without this the panel stays dark
    0x20, 0x02,  // Addressing mode: PAGE. The reference used horizontal (0x00);
                 // this driver flushes a page at a time, and page mode is what
                 // makes the column pointer wrap inside one page instead of
                 // running on into the next.
    0xA1,        // Segment remap: column 127 is SEG0
    0xC8,        // COM scan direction: bottom to top
    0xDA, 0x12,  // [2] COM pin config - patched for a 32-pixel panel
    0x81, 0x3F,  // Contrast
    0xD9, 0x22,  // Pre-charge period
    0xDB, 0x20,  // VCOMH deselect: 0.77 x VCC
    0xA4,        // Display follows RAM, not the all-on test mode
    0xA6,        // Normal, not inverted
    0x2E,        // Scrolling off
    0xAF,        // Display on
};

/** Index into kInitSequence of the multiplex ratio value. */
const size_t kMultiplexValueIndex = 4;

/** Index into kInitSequence of the COM pin configuration value. */
const size_t kComPinsValueIndex = 15;

}  // namespace

HSSD1306Panel::HSSD1306Panel() : device_(nullptr), ready_(false) {
}

HSSD1306Panel::~HSSD1306Panel() {
}

bool HSSD1306Panel::isReady() const {
  return ready_;
}

bool HSSD1306Panel::configure(i2c_master_bus_handle_t bus, uint8_t address, uint32_t speedHz,
                              uint8_t height) {
  ready_ = false;

  if (bus == nullptr) {
    HCritical("no I2C bus given");
    return false;
  }

  i2c_device_config_t deviceConfig = {};
  deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  deviceConfig.device_address = address;
  deviceConfig.scl_speed_hz = speedHz;

  const esp_err_t added = i2c_master_bus_add_device(bus, &deviceConfig, &device_);
  if (added != ESP_OK) {
    HCritical("could not add device 0x%02X to the bus: %s", address, esp_err_to_name(added));
    device_ = nullptr;
    return false;
  }

  // Copied so the height-dependent bytes can be patched without keeping a
  // second table for 128x32 panels. 26 bytes of stack, once.
  uint8_t sequence[sizeof(kInitSequence)];
  memcpy(sequence, kInitSequence, sizeof(kInitSequence));
  sequence[kMultiplexValueIndex] = static_cast<uint8_t>(height - 1);
  sequence[kComPinsValueIndex] = (height > 32) ? 0x12 : 0x02;

  if (!sendCommands(sequence, sizeof(sequence))) {
    HCritical("panel at 0x%02X did not acknowledge - check wiring and address", address);
    return false;
  }

  ready_ = true;
  return true;
}

bool HSSD1306Panel::sendCommands(const uint8_t* commands, size_t length) {
  if (device_ == nullptr || commands == nullptr || length == 0) {
    return false;
  }

  // Commands are few and short, so they are framed on the stack. Display DATA
  // never comes through here - it goes straight out of the framebuffer.
  uint8_t buffer[1 + sizeof(kInitSequence)];
  if (length > sizeof(buffer) - 1) {
    return false;
  }

  buffer[0] = kControlCommand;
  memcpy(buffer + 1, commands, length);

  return i2c_master_transmit(device_, buffer, length + 1, HSSD1306_I2C_TIMEOUT_MS) == ESP_OK;
}

bool HSSD1306Panel::sendCommand(uint8_t command) {
  return sendCommands(&command, 1);
}

bool HSSD1306Panel::sendPage(uint8_t page, const uint8_t* buffer, size_t length) {
  if (!ready_ || buffer == nullptr || length == 0) {
    return false;
  }

  // Page addressing mode: name the page, then park the column pointer at 0 by
  // its two nibbles. The data bytes that follow fill the page exactly.
  const uint8_t position[] = {static_cast<uint8_t>(0xB0 | (page & 0x07)), 0x00, 0x10};
  if (!sendCommands(position, sizeof(position))) {
    return false;
  }

  return i2c_master_transmit(device_, buffer, length, HSSD1306_I2C_TIMEOUT_MS) == ESP_OK;
}

bool HSSD1306Panel::setContrast(uint8_t value) {
  const uint8_t command[] = {0x81, value};
  return sendCommands(command, sizeof(command));
}

bool HSSD1306Panel::setDisplayOn(bool on) {
  return sendCommand(on ? 0xAF : 0xAE);
}

bool HSSD1306Panel::setInverted(bool inverted) {
  return sendCommand(inverted ? 0xA7 : 0xA6);
}
