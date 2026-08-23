#include "HAdcEsp32.hpp"

#define HLOG_MODULE_NAME "HAdc"
#include <HLog/HLog.hpp>

#include <soc/adc_channel.h>

namespace {

/** @brief The library's attenuation, in the driver's own vocabulary. */
adc_atten_t toDriver(HAdcAtten atten) noexcept {
  switch (atten) {
    case HAdcAtten::Atten0db:
      return ADC_ATTEN_DB_0;
    case HAdcAtten::Atten2_5db:
      return ADC_ATTEN_DB_2_5;
    case HAdcAtten::Atten6db:
      return ADC_ATTEN_DB_6;
    case HAdcAtten::Atten12db:
    default:
      return ADC_ATTEN_DB_12;
  }
}

}  // namespace

HAdcEsp32::HAdcEsp32() noexcept
    : unit_(nullptr), calibration_{}, channelAtten_{}, channelReady_{} {}

HAdcEsp32::~HAdcEsp32() {
  // Never actually runs: the backend is a function-local static that outlives
  // everything. Written anyway, because a destructor that leaks in a class
  // nobody destroys is still a destructor that leaks the day somebody does.
  for (adc_cali_handle_t handle : calibration_) {
    if (handle == nullptr) {
      continue;
    }
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_delete_scheme_curve_fitting(handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_delete_scheme_line_fitting(handle);
#endif
  }

  if (unit_ != nullptr) {
    adc_oneshot_del_unit(unit_);
  }
}

int HAdcEsp32::channelFor(int number) noexcept {
  // Built from the chip's own header rather than a table written here, so a
  // different target is a recompile and not an edit. Every ADC1 channel this
  // chip has is listed; the ones it does not have are simply not defined.
#ifdef ADC1_CHANNEL_0_GPIO_NUM
  if (number == ADC1_CHANNEL_0_GPIO_NUM) return 0;
#endif
#ifdef ADC1_CHANNEL_1_GPIO_NUM
  if (number == ADC1_CHANNEL_1_GPIO_NUM) return 1;
#endif
#ifdef ADC1_CHANNEL_2_GPIO_NUM
  if (number == ADC1_CHANNEL_2_GPIO_NUM) return 2;
#endif
#ifdef ADC1_CHANNEL_3_GPIO_NUM
  if (number == ADC1_CHANNEL_3_GPIO_NUM) return 3;
#endif
#ifdef ADC1_CHANNEL_4_GPIO_NUM
  if (number == ADC1_CHANNEL_4_GPIO_NUM) return 4;
#endif
#ifdef ADC1_CHANNEL_5_GPIO_NUM
  if (number == ADC1_CHANNEL_5_GPIO_NUM) return 5;
#endif
#ifdef ADC1_CHANNEL_6_GPIO_NUM
  if (number == ADC1_CHANNEL_6_GPIO_NUM) return 6;
#endif
#ifdef ADC1_CHANNEL_7_GPIO_NUM
  if (number == ADC1_CHANNEL_7_GPIO_NUM) return 7;
#endif
#ifdef ADC1_CHANNEL_8_GPIO_NUM
  if (number == ADC1_CHANNEL_8_GPIO_NUM) return 8;
#endif
#ifdef ADC1_CHANNEL_9_GPIO_NUM
  if (number == ADC1_CHANNEL_9_GPIO_NUM) return 9;
#endif
  return -1;
}

bool HAdcEsp32::ensureUnit() noexcept {
  if (unit_ != nullptr) {
    return true;
  }

  adc_oneshot_unit_init_cfg_t config = {};
  config.unit_id = ADC_UNIT_1;
  config.ulp_mode = ADC_ULP_MODE_DISABLE;

  const esp_err_t result = adc_oneshot_new_unit(&config, &unit_);
  if (result != ESP_OK) {
    unit_ = nullptr;
    HCritical("ADC1 would not start: %s", esp_err_to_name(result));
    return false;
  }

  return true;
}

adc_cali_handle_t HAdcEsp32::ensureCalibration(adc_atten_t atten) noexcept {
  const size_t slot = static_cast<size_t>(atten);
  if (slot >= HADC_ESP32_ATTEN_COUNT) {
    return nullptr;
  }
  if (calibration_[slot] != nullptr) {
    return calibration_[slot];
  }

  adc_cali_handle_t handle = nullptr;
  esp_err_t result = ESP_ERR_NOT_SUPPORTED;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  adc_cali_curve_fitting_config_t config = {};
  config.unit_id = ADC_UNIT_1;
  config.atten = atten;
  config.bitwidth = ADC_BITWIDTH_DEFAULT;
  result = adc_cali_create_scheme_curve_fitting(&config, &handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  adc_cali_line_fitting_config_t config = {};
  config.unit_id = ADC_UNIT_1;
  config.atten = atten;
  config.bitwidth = ADC_BITWIDTH_DEFAULT;
  result = adc_cali_create_scheme_line_fitting(&config, &handle);
#endif

  if (result != ESP_OK) {
    // ESP_ERR_NOT_SUPPORTED here means the calibration eFuses were never
    // burned. Worth a warning rather than a failure: raw counts still work,
    // and readMv() answers -1 rather than inventing a voltage.
    HWarning("no ADC calibration for attenuation %d (%s) - millivolts unavailable",
             static_cast<int>(atten), esp_err_to_name(result));
    return nullptr;
  }

  calibration_[slot] = handle;
  return handle;
}

bool HAdcEsp32::configure(const HAdcPinDesc& pin) noexcept {
  const int channel = channelFor(pin.number);
  if (channel < 0 || channel >= SOC_ADC_MAX_CHANNEL_NUM) {
    // Caught at COMPILE time by HAdcManager's own static_assert; this is the
    // runtime backstop for a board table the assert could not see.
    HCritical("GPIO%d is not an ADC1 pad on this chip", pin.number);
    return false;
  }

  if (!ensureUnit()) {
    return false;
  }

  const adc_atten_t atten = toDriver(pin.atten);

  adc_oneshot_chan_cfg_t config = {};
  config.atten = atten;
  config.bitwidth = ADC_BITWIDTH_DEFAULT;

  const esp_err_t result =
      adc_oneshot_config_channel(unit_, static_cast<adc_channel_t>(channel), &config);
  if (result != ESP_OK) {
    HCritical("'%s' on GPIO%d would not configure: %s", pin.name, pin.number,
              esp_err_to_name(result));
    return false;
  }

  // Built here rather than at the first read, so a chip with no calibration
  // data says so during start-up instead of during a measurement.
  ensureCalibration(atten);

  channelAtten_[channel] = atten;
  channelReady_[channel] = true;

  HDebug("'%s' on GPIO%d is ADC1 channel %d", pin.name, pin.number, channel);
  return true;
}

int HAdcEsp32::readRaw(int number) const noexcept {
  const int channel = channelFor(number);
  if (unit_ == nullptr || channel < 0 || channel >= SOC_ADC_MAX_CHANNEL_NUM ||
      !channelReady_[channel]) {
    return -1;
  }

  int raw = 0;
  if (adc_oneshot_read(unit_, static_cast<adc_channel_t>(channel), &raw) != ESP_OK) {
    return -1;
  }

  return raw;
}

int HAdcEsp32::readMv(int number) const noexcept {
  const int channel = channelFor(number);
  if (channel < 0 || channel >= SOC_ADC_MAX_CHANNEL_NUM || !channelReady_[channel]) {
    return -1;
  }

  const size_t slot = static_cast<size_t>(channelAtten_[channel]);
  if (slot >= HADC_ESP32_ATTEN_COUNT || calibration_[slot] == nullptr) {
    return -1;
  }

  const int raw = readRaw(number);
  if (raw < 0) {
    return -1;
  }

  int millivolts = 0;
  if (adc_cali_raw_to_voltage(calibration_[slot], raw, &millivolts) != ESP_OK) {
    return -1;
  }

  return millivolts;
}
