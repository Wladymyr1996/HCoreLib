#pragma once

/**
 * @file HGpioConfig.h
 * @brief The test build's pin table: a board that exists only in an array.
 *
 * The host backend (HGpioDesktop) is 48 pads of RAM, so these numbers name
 * nothing physical. They are still chosen to look like a real board, because
 * what the suite is testing is the LOGIC that sits between a row of this table
 * and the code above it - inversion, direction, lookup by name - and that logic
 * only means something against a table with the shapes a real one has:
 *
 * - `btn` is the wiring every Hatynka device uses: a switch to ground behind a
 *   pull-up, so the pad reads HIGH when nothing is happening and `invert: true`
 *   turns that back into "pressed is true". HButtonTest drives the PAD and
 *   asserts on the MEANING, which is exactly the translation that would break
 *   silently if inversion ever moved.
 * - `led` is the plain output, `relay` the inverted one, so writes are covered
 *   in both directions.
 * - `sensor` sits in the configurable list rather than the fixed one, so the
 *   table has both kinds and the manager's flat view over the two is tested.
 *
 * @see HGpioManager for the row format.
 */

/** Pins a real board would solder down: the button and the two drivers. */
#define HGPIO_FIXED_PINS(PIN)                  \
  /*  name,    gpio,  dir,     pull,  invert */ \
  PIN("btn",      2,  Input,   Up,    true)     \
  PIN("led",      4,  Output,  None,  false)    \
  PIN("relay",    5,  Output,  None,  true)

/** One pad an installer would decide about, so the second list is not empty. */
#define HGPIO_CONFIGURABLE_PINS(PIN)           \
  /*  name,    gpio,  dir,     pull,  invert */ \
  PIN("sensor",   6,  Input,   Down,  false)
