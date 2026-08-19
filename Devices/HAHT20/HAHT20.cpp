#include "HAHT20.hpp"

#define HLOG_MODULE_NAME "HAHT20"
#include <HLog/HLog.hpp>
#include <HSystemUtils/HSystemUtils.hpp>

namespace {

/** Reads the status register. Sent alone; the sensor replies with one byte. */
const uint8_t kCommandStatus = 0x71;

/**
 * Loads the factory calibration coefficients. The two payload bytes are fixed
 * by the datasheet and carry no meaning of their own.
 */
const uint8_t kCommandCalibrate[] = {0xBE, 0x08, 0x00};

/** Starts one temperature + humidity conversion. The payload is fixed likewise. */
const uint8_t kCommandMeasure[] = {0xAC, 0x33, 0x00};

/** Soft reset. Equivalent to a power cycle, minus the power cycle. */
const uint8_t kCommandReset = 0xBA;

/** Status bit 7: a conversion is running and the result bytes are stale. */
const uint8_t kStatusBusy = 0x80;

/** Status bit 3: the calibration coefficients are loaded. */
const uint8_t kStatusCalibrated = 0x08;

/** Bytes in a reply: status, 20 bits of humidity, 20 bits of temperature, CRC. */
const size_t kReplyLength = 7;

/** How long the calibration command needs before the status bit reads back set. */
const uint32_t kCalibrateMs = 10;

/** How long a soft reset needs. The datasheet says 20 ms; this is that plus margin. */
const uint32_t kResetMs = 25;

/**
 * How many times measure() re-checks a conversion that has not finished.
 *
 * Ten attempts at 2 ms is bounded on purpose: a sensor stuck reporting "busy"
 * costs one reading, never the task it runs in.
 */
const uint8_t kBusyAttempts = 10;

/**
 * The full scale of both readings: 20 bits, so 2^20. Humidity is that scale
 * mapped onto 0..100 %, temperature onto -50..150 C.
 */
const float kFullScale = 1048576.0f;

}  // namespace

HAHT20::HAHT20(i2c_master_bus_handle_t bus, uint8_t address, uint32_t speedHz)
    : bus_(bus), device_(nullptr), address_(address), speedHz_(speedHz), ready_(false) {
}

HAHT20::~HAHT20() {
  // The device handle is ours even though the bus is not, so it is the one
  // thing here worth giving back.
  if (device_ != nullptr) {
    i2c_master_bus_rm_device(device_);
    device_ = nullptr;
  }
}

bool HAHT20::isReady() const {
  return ready_;
}

bool HAHT20::begin() {
  ready_ = false;

  if (bus_ == nullptr) {
    HCritical("no I2C bus given");
    return false;
  }

  if (device_ == nullptr) {
    i2c_device_config_t deviceConfig = {};
    deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    deviceConfig.device_address = address_;
    deviceConfig.scl_speed_hz = speedHz_;

    const esp_err_t added = i2c_master_bus_add_device(bus_, &deviceConfig, &device_);
    if (added != ESP_OK) {
      HCritical("could not add device 0x%02X to the bus: %s", address_, esp_err_to_name(added));
      device_ = nullptr;
      return false;
    }
  }

  // The status read doubles as the presence check: an absent sensor NACKs its
  // own address and this fails without any further traffic.
  uint8_t status = 0;
  if (!readStatus(status)) {
    HCritical("no sensor answering at 0x%02X - check wiring and address", address_);
    return false;
  }

  // A sensor that kept its power across a reboot is already calibrated. Only a
  // cold one is worth the 10 ms, and re-sending the command costs nothing but
  // that - it is idempotent.
  if ((status & kStatusCalibrated) == 0) {
    HInfo("sensor uncalibrated (status 0x%02X) - loading factory coefficients", status);
    if (!calibrate()) {
      return false;
    }
  }

  ready_ = true;
  return true;
}

bool HAHT20::startMeasurement() {
  if (device_ == nullptr) {
    return false;
  }

  const esp_err_t sent = i2c_master_transmit(device_, kCommandMeasure, sizeof(kCommandMeasure),
                                             HAHT20_I2C_TIMEOUT_MS);
  if (sent != ESP_OK) {
    HWarning("measurement command failed: %s", esp_err_to_name(sent));
    return false;
  }

  return true;
}

bool HAHT20::isBusy() {
  uint8_t status = 0;
  if (!readStatus(status)) {
    // Unreachable counts as busy, deliberately: the alternative is telling the
    // caller a result is waiting when the sensor is not even there.
    return true;
  }

  return (status & kStatusBusy) != 0;
}

bool HAHT20::collect(HAHT20Sample& sample) {
  if (device_ == nullptr) {
    return false;
  }

  uint8_t reply[kReplyLength] = {};
  const esp_err_t received =
      i2c_master_receive(device_, reply, sizeof(reply), HAHT20_I2C_TIMEOUT_MS);
  if (received != ESP_OK) {
    HWarning("result read failed: %s", esp_err_to_name(received));
    return false;
  }

  if ((reply[0] & kStatusBusy) != 0) {
    HWarning("still converting - the result bytes are the previous reading");
    return false;
  }

  const uint8_t expected = crc8(reply, kReplyLength - 1);
  if (reply[kReplyLength - 1] != expected) {
    HWarning("CRC mismatch: got 0x%02X, expected 0x%02X", reply[kReplyLength - 1], expected);
    return false;
  }

  // The two readings share a byte: reply[3] carries the bottom four bits of
  // humidity in its high nibble and the top four of temperature in its low one.
  const uint32_t rawHumidity = (static_cast<uint32_t>(reply[1]) << 12) |
                               (static_cast<uint32_t>(reply[2]) << 4) |
                               (static_cast<uint32_t>(reply[3]) >> 4);
  const uint32_t rawTemperature = (static_cast<uint32_t>(reply[3] & 0x0F) << 16) |
                                  (static_cast<uint32_t>(reply[4]) << 8) |
                                  static_cast<uint32_t>(reply[5]);

  sample.humidity = (static_cast<float>(rawHumidity) * 100.0f) / kFullScale;
  sample.temperatureC = ((static_cast<float>(rawTemperature) * 200.0f) / kFullScale) - 50.0f;

  return true;
}

bool HAHT20::measure(HAHT20Sample& sample) {
  if (!ready_) {
    return false;
  }

  if (!startMeasurement()) {
    return false;
  }

  HSystemUtils::sleep(HAHT20_MEASURE_MS);

  // HAHT20_MEASURE_MS is only as precise as the tick the sleep rounds to, and
  // collect() rejects a reading that is not finished - so without this the
  // occasional measurement would simply fail. Asking the sensor costs one
  // status byte.
  for (uint8_t attempt = 0; attempt < kBusyAttempts && isBusy(); ++attempt) {
    HSystemUtils::sleep(2);
  }

  return collect(sample);
}

bool HAHT20::reset() {
  if (device_ == nullptr) {
    return false;
  }

  ready_ = false;

  const esp_err_t sent = i2c_master_transmit(device_, &kCommandReset, 1, HAHT20_I2C_TIMEOUT_MS);
  if (sent != ESP_OK) {
    HWarning("reset command failed: %s", esp_err_to_name(sent));
    return false;
  }

  HSystemUtils::sleep(kResetMs);

  // A reset drops the calibration with everything else, so this goes back
  // through begin() rather than just clearing the flag.
  return begin();
}

bool HAHT20::readStatus(uint8_t& status) {
  if (device_ == nullptr) {
    return false;
  }

  // Command out, one byte back, in a single transaction. i2c_master_transmit
  // followed by i2c_master_receive would release the bus in between and let
  // the display's traffic land in the middle of it.
  const esp_err_t result = i2c_master_transmit_receive(device_, &kCommandStatus, 1, &status, 1,
                                                       HAHT20_I2C_TIMEOUT_MS);
  return result == ESP_OK;
}

bool HAHT20::calibrate() {
  const esp_err_t sent = i2c_master_transmit(device_, kCommandCalibrate, sizeof(kCommandCalibrate),
                                             HAHT20_I2C_TIMEOUT_MS);
  if (sent != ESP_OK) {
    HCritical("calibration command failed: %s", esp_err_to_name(sent));
    return false;
  }

  HSystemUtils::sleep(kCalibrateMs);

  uint8_t status = 0;
  if (!readStatus(status)) {
    HCritical("sensor stopped answering during calibration");
    return false;
  }

  if ((status & kStatusCalibrated) == 0) {
    HCritical("sensor refused to calibrate - status 0x%02X", status);
    return false;
  }

  return true;
}

uint8_t HAHT20::crc8(const uint8_t* data, size_t length) {
  // Polynomial 0x31 (x^8 + x^5 + x^4 + 1), seed 0xFF, MSB first - the same
  // one the SHT3x family uses, which is where this part's protocol comes from.
  uint8_t crc = 0xFF;

  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];

    for (uint8_t bit = 0; bit < 8; ++bit) {
      if ((crc & 0x80) != 0) {
        crc = static_cast<uint8_t>(static_cast<uint8_t>(crc << 1) ^ 0x31);
      } else {
        crc = static_cast<uint8_t>(crc << 1);
      }
    }
  }

  return crc;
}
