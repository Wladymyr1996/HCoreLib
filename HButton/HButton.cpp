#include "HButton.hpp"

HButton::HButton(HGpioPin pin, uint32_t debounceMs, uint32_t longPressMs) noexcept
    : pin_(pin),
      debounce_(debounceMs),
      hold_(longPressMs),
      // Seeded from the PIN, not from "released". An event means a CHANGE, and
      // a button that was already down when this object was built did not
      // change - it was found that way. The case that makes this matter: a
      // device woken from deep sleep BY the button boots with the pad already
      // pressed, and a press event there would act on an input the user has
      // already spent waking the device up.
      settled_(pin.read()),
      longFired_(false),
      pressed_(),
      longPressed_(),
      released_() {
  if (settled_) {
    // Held from the start, so the hold is timed from here: a user who wakes the
    // device by holding the button still gets the long press, and heldMs still
    // means what it says.
    hold_.start();
  }
}

void HButton::onPressed(const HButtonCallback& callback) noexcept {
  pressed_ = callback;
}

void HButton::onLongPressed(const HButtonCallback& callback) noexcept {
  longPressed_ = callback;
}

void HButton::onReleased(const HButtonReleasedCallback& callback) noexcept {
  released_ = callback;
}

void HButton::update() noexcept {
  const bool raw = pin_.read();

  if (raw != settled_) {
    // A candidate edge. The timer runs only for as long as the disagreement
    // lasts, so bounce - which is a disagreement that keeps collapsing - never
    // accumulates towards the threshold.
    if (!debounce_.isRunning()) {
      debounce_.start();
      return;
    }

    if (!debounce_.isExpired()) {
      return;
    }

    settled_ = raw;
    debounce_.stop();

    if (settled_) {
      // hold_ measures the whole press and doubles as the long-press timeout:
      // one timer, because "how long has it been held" and "has it been held
      // long enough" are the same question asked twice.
      hold_.start();
      longFired_ = false;

      if (pressed_.is_valid()) {
        pressed_();
      }
    } else {
      // Stopped BEFORE the callback so heldMs() reports the final duration
      // rather than one that keeps growing while the handler runs.
      hold_.stop();

      if (released_.is_valid()) {
        released_(hold_.elapsedMs());
      }
    }

    return;
  }

  // The raw level agrees with what is believed, so any candidate edge was
  // bounce and is abandoned.
  debounce_.stop();

  if (settled_ && !longFired_ && hold_.isExpired()) {
    longFired_ = true;

    if (longPressed_.is_valid()) {
      longPressed_();
    }
  }
}

bool HButton::isPressed() const noexcept {
  return settled_;
}

uint32_t HButton::heldMs() const noexcept {
  return hold_.elapsedMs();
}
