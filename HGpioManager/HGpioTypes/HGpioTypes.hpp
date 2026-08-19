#pragma once

#include <cstdint>

/**
 * @file HGpioTypes.hpp
 * @brief How a pin is declared. Shared by the board table and every backend.
 *
 * Plain enums and an aggregate, so the whole pin table can be constexpr and
 * checked at compile time rather than at boot.
 */

/** @brief Which way a pin faces. Written unqualified in the board table's rows. */
enum class HGpioDir : uint8_t {
  Input,
  Output,
};

/** @brief The internal resistor an input pin is configured with. Outputs ignore it. */
enum class HGpioPull : uint8_t {
  None,
  Up,
  Down,
};

/**
 * @brief One pin exactly as the board table declares it. Compile-time data.
 *
 * An aggregate with defaults, so `{}` is a valid empty row - which is what lets
 * a board declare no configurable pins at all without leaving a zero-length
 * array behind.
 */
struct HGpioPinDesc {
  /**
   * The pin's name in code - "btn", not "GPIO2". Everything above the manager
   * refers to a pin by name, so moving the button to another pad is a one-line
   * change in the board table and nothing else in the firmware notices.
   */
  const char* name = "";

  /** The chip's pin number. Checked against the target at compile time. */
  int number = -1;

  HGpioDir dir = HGpioDir::Input;
  HGpioPull pull = HGpioPull::None;

  /**
   * What the PHYSICAL level means. `false` is the plain reading: HIGH is true.
   * `true` swaps it, so a switch to ground behind a pull-up reads `true` when
   * it is pressed - and nothing above HGpioPin ever learns the pin was wired
   * that way. Applied in both directions, reads and writes alike.
   */
  bool invert = false;
};
