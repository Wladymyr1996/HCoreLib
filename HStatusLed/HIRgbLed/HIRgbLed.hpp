#pragma once

#include <cstdint>

/**
 * @brief One RGB LED, as HStatusLed needs it: bring it up, show a colour.
 *
 * The seam between the status patterns, which are the same everywhere, and the
 * part that is not: an addressable LED on an RMT channel on the target
 * (HRgbLedEsp32), an array cell a test can read on the host (HRgbLedDesktop).
 * Which one is compiled in is decided in HStatusLed.cpp and nowhere else.
 *
 * Colours arrive already scaled to HSTATUSLED_BRIGHTNESS, in R, G, B order. A
 * backend whose part wants another byte order - WS2812 wants G, R, B - swaps
 * them itself.
 */
class HIRgbLed {
 public:
  virtual ~HIRgbLed();

  /**
   * @brief Claims whatever the LED needs, once, at start-up.
   * @return false when it could not; show() is then a no-op.
   */
  virtual bool begin() noexcept = 0;

  /** @brief Shows one colour until the next call. 0, 0, 0 is dark. */
  virtual void show(uint8_t red, uint8_t green, uint8_t blue) noexcept = 0;
};
