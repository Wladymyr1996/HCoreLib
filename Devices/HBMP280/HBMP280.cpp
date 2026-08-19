#include "HBMP280.hpp"

#include <cmath>
#include <cstring>

#define HLOG_MODULE_NAME "HBMP280"
#include <HLog/HLog.hpp>
#include <HSystemUtils/HSystemUtils.hpp>

namespace {

/** Chip ID register. Constant, readable at any time, and the presence check. */
const uint8_t kRegisterChipId = 0xD0;

/** Soft reset. Writing kResetCommand here is a power cycle in one byte. */
const uint8_t kRegisterReset = 0xE0;

/** Status: bit 3 is converting, bit 0 is copying calibration out of NVM. */
const uint8_t kRegisterStatus = 0xF3;

/** ctrl_meas: temperature oversampling [7:5], pressure [4:2], mode [1:0]. */
const uint8_t kRegisterControl = 0xF4;

/** config: standby [7:5], IIR filter [4:2], 3-wire SPI [0]. */
const uint8_t kRegisterConfig = 0xF5;

/** First of the twenty-four calibration bytes, little-endian from here. */
const uint8_t kRegisterCalibration = 0x88;

/** First of the six result bytes: pressure, then temperature, MSB first. */
const uint8_t kRegisterData = 0xF7;

/** The only value kRegisterReset reacts to. Anything else is ignored. */
const uint8_t kResetCommand = 0xB6;

/** What a BMP280 reports as its chip ID. */
const uint8_t kChipIdBmp280 = 0x58;

/**
 * Two other Bosch parts answer at the same addresses and are worth naming in
 * the log, because getting one of these instead is the usual reason a board
 * that "should work" does not.
 */
const uint8_t kChipIdBme280 = 0x60;
const uint8_t kChipIdBmp180 = 0x55;

/** Status bit 3: a conversion is running. */
const uint8_t kStatusMeasuring = 0x08;

/** Status bit 0: the calibration is still being copied out of NVM. */
const uint8_t kStatusUpdating = 0x01;

/** ctrl_meas mode bits: sleep, or one conversion and back to sleep. */
const uint8_t kModeSleep = 0x00;
const uint8_t kModeForced = 0x01;

const size_t kCalibrationLength = 24;
const size_t kDataLength = 6;

/** How long a soft reset needs before the sensor answers again. */
const uint32_t kResetMs = 10;

/**
 * How many times measure() re-checks a conversion that has not finished.
 *
 * Ten attempts at 2 ms is twenty milliseconds of grace on top of the datasheet
 * time - far more than a working sensor ever needs, and still bounded, so a
 * sensor stuck in "measuring" cannot hold a task forever.
 */
const uint8_t kBusyAttempts = 10;

/** @brief Reads a little-endian 16-bit value out of a calibration block. */
uint16_t readWord(const uint8_t* data, size_t offset) {
  return static_cast<uint16_t>(static_cast<uint16_t>(data[offset]) |
                               (static_cast<uint16_t>(data[offset + 1]) << 8));
}

/**
 * @brief The 20-bit reading packed into three registers.
 *
 * The bottom four bits of the third byte are unused padding - the sensor
 * always sends 24 bits and the low nibble is only meaningful at oversampling
 * settings this driver does not expose.
 */
int32_t readRaw(const uint8_t* data) {
  return static_cast<int32_t>((static_cast<uint32_t>(data[0]) << 12) |
                              (static_cast<uint32_t>(data[1]) << 4) |
                              (static_cast<uint32_t>(data[2]) >> 4));
}

/** @brief Sample count for an oversampling setting: X1 -> 1, X16 -> 16. */
uint32_t sampleCount(HBMP280Oversampling oversampling) {
  return 1u << (static_cast<uint8_t>(oversampling) - 1);
}

}  // namespace

HBMP280::HBMP280(i2c_master_bus_handle_t bus, uint8_t address, uint32_t speedHz)
    : bus_(bus),
      device_(nullptr),
      address_(address),
      speedHz_(speedHz),
      ready_(false),
      temperatureOversampling_(HBMP280Oversampling::X2),
      pressureOversampling_(HBMP280Oversampling::X16),
      filter_(HBMP280Filter::X4),
      calibration_(),
      fine_(0) {
  memset(&calibration_, 0, sizeof(calibration_));
}

HBMP280::~HBMP280() {
  // The device handle is ours even though the bus is not, so it is the one
  // thing here worth giving back.
  if (device_ != nullptr) {
    i2c_master_bus_rm_device(device_);
    device_ = nullptr;
  }
}

bool HBMP280::isReady() const {
  return ready_;
}

bool HBMP280::begin(HBMP280Oversampling temperature, HBMP280Oversampling pressure,
                    HBMP280Filter filter) {
  ready_ = false;
  temperatureOversampling_ = temperature;
  pressureOversampling_ = pressure;
  filter_ = filter;

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

  // The chip ID read doubles as the presence check: an absent sensor NACKs its
  // own address and this fails without any further traffic.
  uint8_t chipId = 0;
  if (!readRegisters(kRegisterChipId, &chipId, 1)) {
    HCritical("no sensor answering at 0x%02X - check wiring, and try 0x%02X", address_,
              (address_ == 0x76) ? 0x77 : 0x76);
    return false;
  }

  if (chipId != kChipIdBmp280) {
    // Naming the part found saves the next hour. All three answer here, and
    // only one of them speaks this driver's register map.
    const char* found = "unknown";
    if (chipId == kChipIdBme280) {
      found = "BME280 - temperature/humidity/pressure, needs its own driver";
    } else if (chipId == kChipIdBmp180) {
      found = "BMP180 - an older part with a different register map";
    }
    HCritical("chip ID 0x%02X at address 0x%02X is not a BMP280 (0x%02X): %s", chipId, address_,
              kChipIdBmp280, found);
    return false;
  }

  // Reset before anything else, so a sensor left mid-conversion by a firmware
  // crash starts from the same state as one that was just powered on.
  if (!writeRegister(kRegisterReset, kResetCommand)) {
    HCritical("reset failed");
    return false;
  }
  HSystemUtils::sleep(kResetMs);

  // The part copies its calibration out of NVM after a reset and must not be
  // read until that finishes. It is well under 2 ms, so the sleep above almost
  // always covers it; this is the guarantee rather than the hope.
  uint8_t status = 0;
  for (uint8_t attempt = 0; attempt < 10; ++attempt) {
    if (!readRegisters(kRegisterStatus, &status, 1)) {
      HCritical("sensor stopped answering after reset");
      return false;
    }
    if ((status & kStatusUpdating) == 0) {
      break;
    }
    HSystemUtils::sleep(2);
  }

  if (!readCalibration()) {
    return false;
  }

  if (!configure()) {
    return false;
  }

  ready_ = true;
  HInfo("BMP280 at 0x%02X ready - conversion takes %u ms", address_,
        static_cast<unsigned>(measureTimeMs()));
  return true;
}

bool HBMP280::startMeasurement() {
  if (device_ == nullptr) {
    return false;
  }

  // Forced mode: one conversion, then the sensor puts itself back to sleep.
  // Writing this register is both the trigger and the settings, which is why
  // the oversampling bits are repeated on every reading rather than set once.
  const uint8_t control = static_cast<uint8_t>(
      (static_cast<uint8_t>(temperatureOversampling_) << 5) |
      (static_cast<uint8_t>(pressureOversampling_) << 2) | kModeForced);

  return writeRegister(kRegisterControl, control);
}

bool HBMP280::isBusy() {
  uint8_t status = 0;
  if (!readRegisters(kRegisterStatus, &status, 1)) {
    // Unreachable counts as busy, deliberately: the alternative is telling the
    // caller a result is waiting when the sensor is not even there.
    return true;
  }

  return (status & kStatusMeasuring) != 0;
}

bool HBMP280::collect(HBMP280Sample& sample) {
  if (device_ == nullptr) {
    return false;
  }

  // Refuse a reading that has not finished. Without this check the data
  // registers still answer - with the RESET value, 0x80000, on the very first
  // conversion after begin(), and with the previous reading afterwards. The
  // first case compensates to a confident ~650 hPa, which is the worst kind of
  // wrong: nothing fails, and a sensible-looking number reaches the screen.
  uint8_t status = 0;
  if (!readRegisters(kRegisterStatus, &status, 1)) {
    HWarning("status read failed");
    return false;
  }

  if ((status & kStatusMeasuring) != 0) {
    HWarning("still converting - the data registers do not hold this reading yet");
    return false;
  }

  uint8_t data[kDataLength] = {};
  if (!readRegisters(kRegisterData, data, sizeof(data))) {
    HWarning("result read failed");
    return false;
  }

  // One burst for both readings, which is not just fewer transfers: the sensor
  // shadows all six registers while they are being read, so a conversion
  // finishing mid-read cannot pair a new pressure with an old temperature.
  const int32_t rawPressure = readRaw(&data[0]);
  const int32_t rawTemperature = readRaw(&data[3]);

  // Temperature first, always - it sets fine_, which the pressure needs.
  const int32_t temperature = compensateTemperature(rawTemperature);

  uint32_t pressure = 0;
  if (!compensatePressure(rawPressure, pressure)) {
    HWarning("calibration cannot compensate this reading - is the sensor initialised?");
    return false;
  }

  sample.temperatureC = static_cast<float>(temperature) / 100.0f;
  sample.pressurePa = static_cast<float>(pressure) / 256.0f;

  return true;
}

bool HBMP280::measure(HBMP280Sample& sample) {
  if (!ready_) {
    return false;
  }

  if (!startMeasurement()) {
    return false;
  }

  HSystemUtils::sleep(measureTimeMs());

  // measureTimeMs() is the datasheet's worst case, but it is only as precise as
  // the tick the sleep rounds to - 10 ms on this build. Asking the sensor is
  // cheaper than assuming, and it is what makes the conversion time a fact
  // rather than an arithmetic guess.
  for (uint8_t attempt = 0; attempt < kBusyAttempts && isBusy(); ++attempt) {
    HSystemUtils::sleep(2);
  }

  return collect(sample);
}

uint32_t HBMP280::measureTimeMs() const {
  // The datasheet's worst case, in hundredths of a millisecond to keep it in
  // integers: 1.25 + 2.3 * t_samples + 2.3 * p_samples + 0.575.
  const uint32_t hundredths = 125 + (230 * sampleCount(temperatureOversampling_)) +
                              (230 * sampleCount(pressureOversampling_)) + 58;

  // Rounded up, then a millisecond of margin - a tick's rounding must never
  // land measure() on a result that has not arrived.
  return ((hundredths + 99) / 100) + 1;
}

bool HBMP280::reset() {
  ready_ = false;
  return begin(temperatureOversampling_, pressureOversampling_, filter_);
}

float HBMP280::altitudeM(float pressurePa, float seaLevelPa) {
  if (pressurePa <= 0.0f || seaLevelPa <= 0.0f) {
    return 0.0f;
  }

  // The international barometric formula, inverted for height.
  return 44330.0f * (1.0f - powf(pressurePa / seaLevelPa, 1.0f / 5.255f));
}

bool HBMP280::readRegisters(uint8_t reg, uint8_t* buffer, size_t length) {
  if (device_ == nullptr || buffer == nullptr || length == 0) {
    return false;
  }

  // Register address out, data back, in a single transaction. A transmit
  // followed by a separate receive would release the bus in between and let
  // the display's traffic land between the address and its own answer.
  const esp_err_t result =
      i2c_master_transmit_receive(device_, &reg, 1, buffer, length, HBMP280_I2C_TIMEOUT_MS);
  return result == ESP_OK;
}

bool HBMP280::writeRegister(uint8_t reg, uint8_t value) {
  if (device_ == nullptr) {
    return false;
  }

  const uint8_t frame[] = {reg, value};
  return i2c_master_transmit(device_, frame, sizeof(frame), HBMP280_I2C_TIMEOUT_MS) == ESP_OK;
}

bool HBMP280::readCalibration() {
  uint8_t data[kCalibrationLength] = {};
  if (!readRegisters(kRegisterCalibration, data, sizeof(data))) {
    HCritical("calibration read failed");
    return false;
  }

  // Little-endian pairs in register order. t1 and p1 are unsigned, every other
  // coefficient is signed - a detail that silently halves the usable pressure
  // range if it is got wrong, because the reading stays plausible.
  calibration_.t1 = readWord(data, 0);
  calibration_.t2 = static_cast<int16_t>(readWord(data, 2));
  calibration_.t3 = static_cast<int16_t>(readWord(data, 4));
  calibration_.p1 = readWord(data, 6);
  calibration_.p2 = static_cast<int16_t>(readWord(data, 8));
  calibration_.p3 = static_cast<int16_t>(readWord(data, 10));
  calibration_.p4 = static_cast<int16_t>(readWord(data, 12));
  calibration_.p5 = static_cast<int16_t>(readWord(data, 14));
  calibration_.p6 = static_cast<int16_t>(readWord(data, 16));
  calibration_.p7 = static_cast<int16_t>(readWord(data, 18));
  calibration_.p8 = static_cast<int16_t>(readWord(data, 20));
  calibration_.p9 = static_cast<int16_t>(readWord(data, 22));

  // All-zero or all-ones means the read succeeded electrically and returned
  // nothing real - a floating SDO, or a clone with an empty NVM. Both would
  // otherwise produce confident, wrong pressures forever.
  if ((calibration_.t1 == 0 && calibration_.p1 == 0) ||
      (calibration_.t1 == 0xFFFF && calibration_.p1 == 0xFFFF)) {
    HCritical("calibration reads as 0x%04X/0x%04X - not a working sensor", calibration_.t1,
              calibration_.p1);
    return false;
  }

  return true;
}

bool HBMP280::configure() {
  // config is only reliably writable while the sensor is asleep, and it is
  // asleep now because begin() has just reset it. Standby is left at its
  // shortest setting: it governs normal mode, which forced mode never enters.
  const uint8_t config = static_cast<uint8_t>(static_cast<uint8_t>(filter_) << 2);
  if (!writeRegister(kRegisterConfig, config)) {
    HCritical("config register write failed");
    return false;
  }

  // Oversampling written with mode = sleep. startMeasurement() writes the same
  // bits again with mode = forced; this is here so the settings are in the
  // sensor even if the first reading never comes.
  const uint8_t control = static_cast<uint8_t>(
      (static_cast<uint8_t>(temperatureOversampling_) << 5) |
      (static_cast<uint8_t>(pressureOversampling_) << 2) | kModeSleep);
  if (!writeRegister(kRegisterControl, control)) {
    HCritical("control register write failed");
    return false;
  }

  return true;
}

int32_t HBMP280::compensateTemperature(int32_t raw) {
  // Bosch's fixed-point compensation, transcribed from the datasheet and
  // deliberately not rearranged: the shifts are where the rounding happens,
  // and "simplifying" them moves the result by tenths of a degree.
  const int32_t var1 =
      (((raw >> 3) - (static_cast<int32_t>(calibration_.t1) << 1)) *
       static_cast<int32_t>(calibration_.t2)) >>
      11;
  const int32_t var2 =
      ((((((raw >> 4) - static_cast<int32_t>(calibration_.t1)) *
          ((raw >> 4) - static_cast<int32_t>(calibration_.t1))) >>
         12) *
        static_cast<int32_t>(calibration_.t3)) >>
       14);

  fine_ = var1 + var2;

  return (fine_ * 5 + 128) >> 8;
}

bool HBMP280::compensatePressure(int32_t raw, uint32_t& pressure) const {
  // The 64-bit variant. The 32-bit one in the datasheet is for parts without a
  // 64-bit type and gives up about 1 Pa of accuracy; a RISC-V core has no
  // reason to take that trade.
  int64_t var1 = static_cast<int64_t>(fine_) - 128000;
  int64_t var2 = var1 * var1 * static_cast<int64_t>(calibration_.p6);
  var2 = var2 + ((var1 * static_cast<int64_t>(calibration_.p5)) << 17);
  var2 = var2 + (static_cast<int64_t>(calibration_.p4) << 35);
  var1 = ((var1 * var1 * static_cast<int64_t>(calibration_.p3)) >> 8) +
         ((var1 * static_cast<int64_t>(calibration_.p2)) << 12);
  var1 = (((static_cast<int64_t>(1) << 47) + var1) * static_cast<int64_t>(calibration_.p1)) >> 33;

  if (var1 == 0) {
    return false;  // Division by zero, which means p1 is zero: no calibration.
  }

  int64_t result = 1048576 - raw;
  result = (((result << 31) - var2) * 3125) / var1;
  var1 = (static_cast<int64_t>(calibration_.p9) * (result >> 13) * (result >> 13)) >> 25;
  var2 = (static_cast<int64_t>(calibration_.p8) * result) >> 19;
  result = ((result + var1 + var2) >> 8) + (static_cast<int64_t>(calibration_.p7) << 4);

  pressure = static_cast<uint32_t>(result);
  return true;
}
