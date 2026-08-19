#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/i2c_master.h>

/** Default 7-bit I2C address. Modules with the jumper moved use 0x3D. */
#ifndef HSSD1306_ADDRESS
#define HSSD1306_ADDRESS 0x3C
#endif

/** I2C clock. 400 kHz is fast mode, which every one of these panels supports. */
#ifndef HSSD1306_I2C_SPEED_HZ
#define HSSD1306_I2C_SPEED_HZ 400000
#endif

/** How long any single I2C transfer may take before it is called a failure. */
#ifndef HSSD1306_I2C_TIMEOUT_MS
#define HSSD1306_I2C_TIMEOUT_MS 100
#endif

/**
 * @brief Everything an SSD1306 panel does that does not depend on its size.
 *
 * The I2C device, the init sequence, command framing and the page write. It
 * exists so that HSSD1306<W, H> - which is a template, and therefore
 * duplicated for every panel size in the firmware - contains only the
 * framebuffer and the drawing, and this code is compiled exactly once.
 *
 * Not useful on its own. Instantiate HSSD1306<WIDTH, HEIGHT>.
 */
class HSSD1306Panel {
 public:
  HSSD1306Panel();
  virtual ~HSSD1306Panel();

  HSSD1306Panel(const HSSD1306Panel&) = delete;
  HSSD1306Panel& operator=(const HSSD1306Panel&) = delete;

  /** @brief True once the panel has been configured and acknowledged it. */
  bool isReady() const;

  /**
   * @brief Panel width in pixels.
   *
   * Virtual because the size is a TEMPLATE parameter of the derived class and
   * this base is deliberately not templated - it is the half of the driver that
   * is compiled once. Code that draws can therefore ask the panel how big it is
   * instead of being told twice, once by the template argument and once by a
   * constant of its own that nothing stops from disagreeing.
   */
  virtual uint16_t width() const = 0;

  /** @brief Panel height in pixels. Virtual for the same reason as width(). */
  virtual uint16_t height() const = 0;

  /** @brief Sets contrast, 0x00 dimmest to 0xFF brightest. The init sequence uses 0x3F. */
  bool setContrast(uint8_t value);

  /** @brief Sleeps or wakes the panel. Display RAM survives; current drops to ~1 uA. */
  bool setDisplayOn(bool on);

  /** @brief Inverts every pixel in hardware, without touching the framebuffer. */
  bool setInverted(bool inverted);

 protected:
  /**
   * @brief Adds the panel to an I2C bus and runs the init sequence.
   *
   * @param bus     An open bus from i2c_new_master_bus(). BORROWED: it must
   *                outlive this object, and is never deleted here - a sensor
   *                on the same two wires is the normal case.
   * @param address 7-bit I2C address.
   * @param speedHz Clock for THIS device; others on the bus keep their own.
   * @param height  Panel height in pixels, which decides the multiplex ratio
   *                and the COM pin configuration.
   * @return false if the device could not be added or the panel did not
   *         acknowledge. The first transfer doubles as the presence check:
   *         an absent panel NACKs its address.
   */
  bool configure(i2c_master_bus_handle_t bus, uint8_t address, uint32_t speedHz, uint8_t height);

  /** @brief Writes one command byte, framed with the 0x00 control byte. */
  bool sendCommand(uint8_t command);

  /** @brief Writes a run of command bytes in one transfer. */
  bool sendCommands(const uint8_t* commands, size_t length);

  /**
   * @brief Points the controller at a page, then writes that page's bytes.
   *
   * @param page   Page index, 0-7.
   * @param buffer Must already begin with the 0x40 data control byte - it is
   *               transmitted verbatim. See HSSD1306's framebuffer layout for
   *               why the prefix lives inside the buffer.
   * @param length Bytes to transmit, control byte included.
   */
  bool sendPage(uint8_t page, const uint8_t* buffer, size_t length);

 private:
  i2c_master_dev_handle_t device_;
  bool ready_;
};
