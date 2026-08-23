#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @file HAdcTypes.hpp
 * @brief How an analog pin is declared. Shared by the board table and the backends.
 *
 * A plain enum and an aggregate, so the whole analog table can be constexpr and
 * checked at compile time rather than at boot - exactly as HGpioTypes.hpp does
 * for digital pins.
 */

/**
 * @brief How far the input is attenuated before it reaches the converter.
 *
 * The ranges are the ESP32-C6's and are approximate - the ends of each are
 * non-linear, which is what calibration corrects. Pick the smallest one that
 * covers the signal: a wider range spends resolution on volts that will never
 * arrive.
 *
 * **The pad must never exceed VDDA (3.3 V) whatever this says.** Attenuation
 * changes what the converter can MEASURE, not what the pin can survive - that
 * is a divider's job.
 */
enum class HAdcAtten : uint8_t {
  Atten0db,    ///< about 0-950 mV
  Atten2_5db,  ///< about 0-1250 mV
  Atten6db,    ///< about 0-1750 mV
  Atten12db,   ///< about 0-3100 mV. The usual choice behind a divider.
};

/**
 * @brief One analog pin exactly as the board table declares it. Compile-time data.
 *
 * An aggregate with defaults, so `{}` is a valid empty row - which is what lets
 * a board declare no analog pins at all without leaving a zero-length array
 * behind.
 */
struct HAdcPinDesc {
  /**
   * The pin's name in code - "vbat", not "GPIO6". Everything above the manager
   * refers to a channel by name, so moving the divider to another pad is a
   * one-line change in the board table and nothing else notices.
   */
  const char* name = "";

  /** The chip's pin number. Checked against this chip's ADC pads at compile time. */
  int number = -1;

  HAdcAtten atten = HAdcAtten::Atten12db;

  /**
   * What the resistor divider in front of this pad divides BY.
   *
   * 1.0 for a pin wired straight to the signal; 2.0 for the usual equal-value
   * pair. HAdcChannel::readMv() multiplies by it, so what a caller gets back is
   * the voltage at the SOURCE - and nothing above HAdcChannel ever learns there
   * were resistors, exactly as nothing above HGpioPin learns a switch was
   * wired to ground.
   *
   * A divider is a fact about the board, which is why it is declared here
   * beside the pad rather than in application configuration.
   */
  float divider = 1.0f;
};
