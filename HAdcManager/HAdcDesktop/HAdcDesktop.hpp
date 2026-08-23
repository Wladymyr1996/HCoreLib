#pragma once

#include "../HIAdc/HIAdc.hpp"

/** How many pads the host backend pretends to have. */
#ifndef HADC_DESKTOP_PIN_COUNT
#define HADC_DESKTOP_PIN_COUNT 16
#endif

/**
 * @brief A converter made of an array, so the logic above can be tested.
 *
 * Every configured pad holds a millivolt value a test sets directly. That is
 * the whole of it - and it is what lets the divider arithmetic, the median and
 * a battery curve be driven across their entire range on a desktop, including
 * the ends no bench supply is convenient for.
 *
 * setMv() is the test's way in and does not exist on the target, so nothing in
 * application code can reach for it by accident.
 */
class HAdcDesktop : public HIAdc {
 public:
  HAdcDesktop() noexcept;

  bool configure(const HAdcPinDesc& pin) noexcept override;
  int readRaw(int number) const noexcept override;
  int readMv(int number) const noexcept override;

  // -- what only a test uses ------------------------------------------------

  /** @brief Sets what this pad will report, in millivolts at the pad. */
  void setMv(int number, int millivolts) noexcept;

  /**
   * @brief Makes the next reads fail, the way a broken converter would.
   *
   * A reading that is not available must come back as -1 rather than as a
   * plausible zero, and the only way to be sure of that is to arrange it.
   */
  void setFailing(bool failing) noexcept;

  /** @brief Forgets every configured pad, so one test cannot lean on another. */
  void reset() noexcept;

 private:
  bool isUsable(int number) const noexcept;

  bool configured_[HADC_DESKTOP_PIN_COUNT];
  int millivolts_[HADC_DESKTOP_PIN_COUNT];
  bool failing_;
};
