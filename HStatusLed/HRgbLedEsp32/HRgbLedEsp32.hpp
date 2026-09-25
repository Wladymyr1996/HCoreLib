#pragma once

#include <cstdint>

#include "../HIRgbLed/HIRgbLed.hpp"

/**
 * @brief One WS2812-type addressable LED on an RMT TX channel.
 *
 * The on-board RGB LED of the ESP32-C6 boards this family uses. The pad is
 * HSTATUSLED_GPIO, from the application's HGpioConfig.h - not a HGpioManager row,
 * because the RMT peripheral owns the pad and HGpioManager would fight it for
 * it.
 *
 * ## The wire
 * 24 bits, most significant first, in HSTATUSLED_COLOR_ORDER (G, R, B on a
 * WS2812). A 0 is 0.3 us high then 0.9 us low; a 1 is 0.9 us high then 0.3 us
 * low - the middle of the datasheet's windows, on a 10 MHz RMT clock. The line
 * idles low between frames, and nothing writes the LED more often than every
 * 50 ms, so the latch gap (50 us on the old part, 280 us on the B revision) is
 * always met without sending one.
 *
 * ## Allocation
 * The channel and the encoder are allocated by IDF in begin(), once, at
 * start-up. Nothing is allocated after that.
 *
 * Compiled only when HSTATUSLED_ENABLE is 1, so a build without the LED links no
 * RMT code at all.
 */
class HRgbLedEsp32 : public HIRgbLed {
 public:
  bool begin() noexcept override;
  void show(uint8_t red, uint8_t green, uint8_t blue) noexcept override;

 private:
  /** IDF's handles, kept opaque so this header names no ESP type. */
  void* channel_ = nullptr;
  void* encoder_ = nullptr;

  /** One frame. A member, because RMT reads it after rmt_transmit() returns. */
  uint8_t frame_[3] = {};
};
