#include "HRgbLedEsp32.hpp"

#include <HStatusLed/HStatusLed.hpp>

#if HSTATUSLED_ENABLE

#define HLOG_MODULE_NAME "Led"
#include <HLog/HLog.hpp>

#include <HGpioConfig.h>

#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>

#ifndef HSTATUSLED_GPIO
#error "HSTATUSLED_ENABLE is 1 but HSTATUSLED_GPIO is not defined - name the LED's pad in HGpioConfig.h"
#endif

namespace {

/** 10 MHz: one RMT tick is 0.1 us, fine enough for the WS2812's 0.3/0.9 us. */
constexpr uint32_t kResolutionHz = 10000000;

/** 0.3 us and 0.9 us, in RMT ticks. */
constexpr uint16_t kShortTicks = 3;
constexpr uint16_t kLongTicks = 9;

/** How long show() waits for a 30 us frame to leave before giving up. */
constexpr int kSendTimeoutMs = 10;

rmt_channel_handle_t asChannel(void* handle) noexcept {
  return static_cast<rmt_channel_handle_t>(handle);
}

rmt_encoder_handle_t asEncoder(void* handle) noexcept {
  return static_cast<rmt_encoder_handle_t>(handle);
}

}  // namespace

bool HRgbLedEsp32::begin() noexcept {
  if (channel_ != nullptr) {
    return true;
  }

  rmt_tx_channel_config_t channelConfig = {};
  channelConfig.gpio_num = static_cast<gpio_num_t>(HSTATUSLED_GPIO);
  channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
  channelConfig.resolution_hz = kResolutionHz;
  // 48 symbols is one channel's whole block on the C6; a frame is 24.
  channelConfig.mem_block_symbols = 48;
  channelConfig.trans_queue_depth = 2;

  rmt_channel_handle_t channel = nullptr;
  esp_err_t result = rmt_new_tx_channel(&channelConfig, &channel);
  if (result != ESP_OK) {
    HCritical("no RMT channel for the status LED on GPIO%d (%d)",
              static_cast<int>(HSTATUSLED_GPIO), static_cast<int>(result));
    return false;
  }

  rmt_bytes_encoder_config_t encoderConfig = {};
  encoderConfig.bit0.level0 = 1;
  encoderConfig.bit0.duration0 = kShortTicks;
  encoderConfig.bit0.level1 = 0;
  encoderConfig.bit0.duration1 = kLongTicks;
  encoderConfig.bit1.level0 = 1;
  encoderConfig.bit1.duration0 = kLongTicks;
  encoderConfig.bit1.level1 = 0;
  encoderConfig.bit1.duration1 = kShortTicks;
  encoderConfig.flags.msb_first = 1;

  rmt_encoder_handle_t encoder = nullptr;
  result = rmt_new_bytes_encoder(&encoderConfig, &encoder);
  if (result != ESP_OK) {
    HCritical("no RMT encoder for the status LED (%d)", static_cast<int>(result));
    rmt_del_channel(channel);
    return false;
  }

  result = rmt_enable(channel);
  if (result != ESP_OK) {
    HCritical("could not enable the status LED's RMT channel (%d)", static_cast<int>(result));
    rmt_del_encoder(encoder);
    rmt_del_channel(channel);
    return false;
  }

  channel_ = channel;
  encoder_ = encoder;
  return true;
}

void HRgbLedEsp32::show(uint8_t red, uint8_t green, uint8_t blue) noexcept {
  if (channel_ == nullptr) {
    return;
  }

  // The previous frame may still be leaving, and it reads frame_: wait for it
  // before overwriting. At 30 us a frame, this is never a real wait.
  rmt_tx_wait_all_done(asChannel(channel_), kSendTimeoutMs);

#if HSTATUSLED_COLOR_ORDER == HSTATUSLED_ORDER_RGB
  frame_[0] = red;
  frame_[1] = green;
  frame_[2] = blue;
#else
  frame_[0] = green;
  frame_[1] = red;
  frame_[2] = blue;
#endif

  rmt_transmit_config_t sendConfig = {};
  sendConfig.loop_count = 0;

  const esp_err_t result = rmt_transmit(asChannel(channel_), asEncoder(encoder_), frame_,
                                        sizeof(frame_), &sendConfig);
  if (result != ESP_OK) {
    HWarning("status LED frame not sent (%d)", static_cast<int>(result));
  }
}

#endif  // HSTATUSLED_ENABLE
