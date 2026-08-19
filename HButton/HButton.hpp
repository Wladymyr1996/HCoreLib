#pragma once

#include <cstdint>

#include <etl/delegate.h>

#include <HGpioManager/HGpioPin/HGpioPin.hpp>
#include <HITickable/HITickable.hpp>
#include <HTimer/HTimer.hpp>

/** @brief How long one level must hold still before it is believed, in ms. */
#ifndef HBUTTON_DEBOUNCE_MS
#define HBUTTON_DEBOUNCE_MS 30
#endif

/** @brief How long a press must last to count as a long press, in ms. */
#ifndef HBUTTON_LONG_PRESS_MS
#define HBUTTON_LONG_PRESS_MS 3000
#endif

/** @brief Fired on press and on long press. Takes nothing, returns nothing. */
using HButtonCallback = etl::delegate<void()>;

/** @brief Fired on release, carrying how long the button was held, in ms. */
using HButtonReleasedCallback = etl::delegate<void(uint32_t)>;

/**
 * @brief Debounces one pin and turns it into pressed / long-pressed / released.
 *
 * Takes an HGpioPin that is already configured and already speaks LOGICAL
 * levels, so this class contains not one word about pull-ups, inversion or pad
 * numbers: `pin.read() == true` means pressed, whatever the wiring does.
 *
 * ## What update() has to be given
 * Nothing but the CPU, about every HCORELIB_TICK_MS - see HITickable. The tick sets
 * how quickly an edge is NOTICED; every duration this class reports is measured
 * with HTimer against an absolute clock, so a late or missed tick costs
 * latency and never accuracy. A press held for 3000 ms is reported as 3000 ms
 * even if the task that drives it stalled in the middle of it.
 *
 * ## The debounce rule
 * A level that disagrees with the settled one starts a timer; agreement cancels
 * it. Only a disagreement that survives HBUTTON_DEBOUNCE_MS becomes the new
 * settled level and fires an event. Contact bounce is exactly a disagreement
 * that does not survive, so it produces no events at all rather than a burst
 * of them.
 *
 * ## The events
 * | Callback | When |
 * | --- | --- |
 * | `onPressed` | the moment a press is believed |
 * | `onLongPressed` | ONCE, HBUTTON_LONG_PRESS_MS into a press that is still held |
 * | `onReleased(heldMs)` | the moment a release is believed, with the press duration |
 *
 * `onReleased` fires for every press, long ones included, and carries the
 * duration so the application decides what a "click" is rather than having a
 * second policy baked in here. `onLongPressed` fires while the button is still
 * down - which is what makes "hold three seconds to reset" feel right: the
 * device acts, and the user lets go afterwards.
 *
 * Callbacks run inside update(), in the caller's task. Keep them short, and do
 * not block in one.
 *
 * @code
 *   HButton button(HGpioManager::find("btn"));
 *   button.onLongPressed(HButtonCallback::create<&onHold>());
 *   // ... every HCORELIB_TICK_MS:
 *   button.update();
 * @endcode
 */
class HButton : public HITickable {
 public:
  /**
   * @param pin A configured pin whose logical true means pressed. An invalid
   *        handle is accepted and simply never reports anything. The button
   *        adopts the pin's CURRENT level without firing for it - a button
   *        already down when this is built (a device woken by it) reports no
   *        press, only the long press and the release that follow. Ask
   *        isPressed() if the state at start-up matters.
   * @param debounceMs How long a level must hold still to be believed.
   * @param longPressMs How long a press must last to count as long.
   */
  explicit HButton(HGpioPin pin,
                   uint32_t debounceMs = HBUTTON_DEBOUNCE_MS,
                   uint32_t longPressMs = HBUTTON_LONG_PRESS_MS) noexcept;

  /** @brief Sets the press callback, replacing any previous one. */
  void onPressed(const HButtonCallback& callback) noexcept;

  /** @brief Sets the long-press callback, replacing any previous one. */
  void onLongPressed(const HButtonCallback& callback) noexcept;

  /** @brief Sets the release callback, replacing any previous one. */
  void onReleased(const HButtonReleasedCallback& callback) noexcept;

  /** @brief Samples the pin, settles it, and fires whatever that implies. */
  void update() noexcept override;

  /** @brief The settled state - not the raw pin, which may be mid-bounce. */
  bool isPressed() const noexcept;

  /** @brief How long the current press has lasted, or the last one did, in ms. */
  uint32_t heldMs() const noexcept;

 private:
  HGpioPin pin_;

  HTimer debounce_;   ///< Runs only while the raw level disagrees with settled_.
  HTimer hold_;       ///< Runs for the whole of a settled press; also measures heldMs().

  bool settled_;      ///< The level currently believed.
  bool longFired_;    ///< Stops onLongPressed repeating for the rest of one press.

  HButtonCallback pressed_;
  HButtonCallback longPressed_;
  HButtonReleasedCallback released_;
};
