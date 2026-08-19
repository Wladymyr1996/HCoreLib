#pragma once

#include "../HGpioTypes/HGpioTypes.hpp"

/**
 * @brief A handle to one declared pin, speaking LOGICAL levels.
 *
 * This is the type everything above the GPIO layer holds. It is a pointer to
 * the board table's compile-time row, so it is cheap to copy, safe to store in
 * a member, and can never dangle - the row it points at is constexpr and
 * outlives the program.
 *
 * ## Inversion stops here
 * read() and write() apply HGpioPinDesc::invert, so a value crossing this class
 * is always what it MEANS - "button pressed", "valve demanded" - never what the
 * pad happens to read. A switch to ground behind a pull-up is one `true` in the
 * board table, and HButton above it contains not one word about wiring.
 *
 * A default-constructed handle is INVALID, which is what HGpioManager::find()
 * returns for a name it does not know: read() then answers false and write() is
 * a no-op, so a typo in a pin name degrades to a pin that never does anything
 * rather than to a crash.
 */
class HGpioPin {
 public:
  /** @brief Creates an invalid handle. */
  HGpioPin() noexcept;

  /** @brief Wraps one row of the board table. Not called by application code - see HGpioManager::find(). */
  explicit HGpioPin(const HGpioPinDesc* desc) noexcept;

  /** @brief False for a handle that names no pin. */
  bool isValid() const noexcept;

  /**
   * @brief The pin's LOGICAL level, inversion applied.
   * @return false for an invalid handle.
   */
  bool read() const noexcept;

  /**
   * @brief Drives the pin to a LOGICAL level, inversion applied.
   *
   * Does nothing for an invalid handle or an input pin: an input that could be
   * driven is a short circuit waiting for the first careless caller.
   */
  void write(bool value) const noexcept;

  /** @brief The name the board table gave this pin, or "" when invalid. */
  const char* name() const noexcept;

  /** @brief The chip pin number, or -1 when invalid. Diagnostics only. */
  int number() const noexcept;

  /** @brief True if the pin was declared as an output. */
  bool isOutput() const noexcept;

  /**
   * @brief True when the pin's logical `true` is the pad's LOW level.
   *
   * The board table's `invert`, under the name that matters to code deciding
   * what the HARDWARE should do with the pad - arming a wake source, say, which
   * has to be told a level rather than a meaning.
   */
  bool isActiveLow() const noexcept;

 private:
  const HGpioPinDesc* desc_;
};
