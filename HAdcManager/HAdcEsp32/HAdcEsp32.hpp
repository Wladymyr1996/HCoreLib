#pragma once

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_oneshot.h>
#include <soc/soc_caps.h>

#include "../HIAdc/HIAdc.hpp"

/**
 * Distinct attenuations. There are four (adc_atten_t is 0..3), and one
 * calibration handle is created per attenuation actually used - not per pin,
 * because two pads at the same attenuation share the same curve.
 */
#define HADC_ESP32_ATTEN_COUNT 4

/**
 * @brief ADC1 on the ESP32, one conversion at a time.
 *
 * ## Oneshot, not continuous, and that is a decision
 * Continuous mode runs a DMA-fed conversion stream, which is what audio-rate
 * sampling needs. Nothing in this ecosystem samples at audio rate: a battery is
 * read once a minute on a node that spends the rest of that minute asleep.
 * Oneshot costs tens of microseconds, needs no task and no ring buffer, and may
 * be called from a tick. A device that ever needs the other one wants a
 * different class, not a mode on this one.
 *
 * ## ADC1 only
 * ADC2 shares its converter with the Wi-Fi radio on every chip that has both,
 * so a reading taken while the radio is up can simply fail. A battery gauge
 * that stops working exactly when the node is transmitting is worse than no
 * gauge, and there is nothing here that could arbitrate it.
 *
 * ## Calibration is per attenuation and is created once
 * The scheme's handle belongs to a (unit, attenuation) pair, not to a pad. It
 * is built in configure() the first time an attenuation is seen and kept for
 * the life of the firmware - which is what lets HAdcChannel stay a cheap value
 * type carrying none of it.
 *
 * A chip with no calibration data burned into its eFuses reports millivolts as
 * -1 rather than handing back raw counts wearing a millivolt label.
 */
class HAdcEsp32 : public HIAdc {
 public:
  HAdcEsp32() noexcept;
  ~HAdcEsp32() override;

  bool configure(const HAdcPinDesc& pin) noexcept override;
  int readRaw(int number) const noexcept override;
  int readMv(int number) const noexcept override;

 private:
  /** @brief Brings the oneshot unit up on first use. */
  bool ensureUnit() noexcept;

  /** @brief The calibration handle for one attenuation, created on first sight. */
  adc_cali_handle_t ensureCalibration(adc_atten_t atten) noexcept;

  /** @brief The ADC1 channel a pad is wired to, or -1 when it is not an ADC1 pad. */
  static int channelFor(int number) noexcept;

  adc_oneshot_unit_handle_t unit_;

  /** Indexed by adc_atten_t. Null until that attenuation is used, or uncalibratable. */
  adc_cali_handle_t calibration_[HADC_ESP32_ATTEN_COUNT];

  /** Which attenuation each configured channel was given, for readMv(). */
  adc_atten_t channelAtten_[SOC_ADC_MAX_CHANNEL_NUM];

  bool channelReady_[SOC_ADC_MAX_CHANNEL_NUM];
};
