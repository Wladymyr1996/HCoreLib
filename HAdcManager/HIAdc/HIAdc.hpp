#pragma once

#include "../HAdcTypes/HAdcTypes.hpp"

/**
 * @brief The converter-level contract: configure a pad, read counts, read millivolts.
 *
 * Deliberately the smallest thing that can be implemented twice, exactly as
 * HIGpio is. It speaks in PHYSICAL millivolts at the PAD and in pin numbers -
 * names, the board table and the divider all live above it, in HAdcChannel and
 * HAdcManager - so a backend is a few lines of platform call with no policy to
 * get wrong.
 *
 * ## Why millivolts are down here and not up there
 * Turning counts into millivolts needs the chip's own calibration data, which
 * on the ESP32 is a per-(unit, attenuation) handle owned by the driver and on a
 * desktop is a number a test made up. Neither is something a caller can supply,
 * so the conversion belongs on this side of the seam. What is NOT here is the
 * divider: that is a property of the board, not of the converter.
 *
 * Exactly one implementation is compiled into a given build, selected by
 * IS_MCU. Do not name a backend in application code: go through
 * HAdcManager::find() and the HAdcChannel it hands back.
 */
class HIAdc {
 public:
  virtual ~HIAdc();

  /**
   * @brief Claims a pad for the converter and applies its attenuation.
   * @return false if the platform rejected the pin.
   */
  virtual bool configure(const HAdcPinDesc& pin) noexcept = 0;

  /**
   * @brief One conversion, in raw counts.
   * @return -1 when the pin is not configured or the read failed. Never a
   *         plausible number for something that was not measured.
   */
  virtual int readRaw(int number) const noexcept = 0;

  /**
   * @brief One conversion, calibrated, in millivolts AT THE PAD.
   * @return -1 when the pin is not configured, the read failed, or this build
   *         has no calibration for it - a device that cannot calibrate reports
   *         nothing rather than raw counts wearing a millivolt label.
   */
  virtual int readMv(int number) const noexcept = 0;
};
