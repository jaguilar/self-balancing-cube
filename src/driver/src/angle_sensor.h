#pragma once

#include <cstdint>

#include "common/base_classes/Sensor.h"
#include "stm32_hal.h"

namespace stfoc {

struct SpiPinConfig {
  hal_gpio_t port;
  uint32_t pin;
  hal_gpio_af_t alternate;
};

struct EncoderChannelConfig {
  hal_gpio_t port;
  uint32_t pin;
  hal_gpio_af_t alternate;
  hal_tim_channel_t channel;
  hal_tim_ic_capture_unit_t capture_unit;
  hal_tim_channel_src_t input_source;
};

struct As5047pSensorConfig {
  hal_spi_t spi_instance;
  SpiPinConfig sck;
  SpiPinConfig miso;
  SpiPinConfig mosi;
  SpiPinConfig nss;
  hal_spi_baud_rate_prescaler_t spi_prescaler;
  hal_tim_t timer_instance;
  EncoderChannelConfig enc_a;
  EncoderChannelConfig enc_b;
  uint32_t startup_delay_ms = 10U;
  uint32_t spi_timeout_ms = 10U;
};

/// AS5047P angle sensor driver using SPI for initialization/configuration and
/// a hardware quadrature timer (ABI) for low-latency runtime angle tracking.
class As5047pAngleSensor final : public Sensor {
 public:
  static constexpr uint32_t kCountsPerRev = 4096U;
  static constexpr uint32_t kCountMask = kCountsPerRev - 1U;

  enum class Error : uint8_t {
    kNone = 0,
    kSpiInitFailed,
    kSpiTransferFailed,
    kParityError,
    kSensorErrorFlag,
    kSettings1VerifyFailed,
    kSettings2VerifyFailed,
    kCordicOverflow,
    kDiagNotReady,
    kTimerInitFailed,
  };

  explicit As5047pAngleSensor(const As5047pSensorConfig& config);
  ~As5047pAngleSensor();

  As5047pAngleSensor(const As5047pAngleSensor&) = delete;
  As5047pAngleSensor& operator=(const As5047pAngleSensor&) = delete;

  void init() override;
  void Deinit();
  void update() override;

  float getSensorAngle() override;
  float getMechanicalAngle() override;
  float getAngle() override;
  double getPreciseAngle() override;
  float getVelocity() override;
  int32_t getFullRotations() override;
  int needsSearch() override { return 0; }

  bool initialized() const { return initialized_; }
  Error last_error() const { return last_error_; }
  static const char* ErrorToString(Error error);
  uint16_t last_diagnostics() const { return last_diagnostics_; }
  int64_t total_count() const { return total_count_; }
  uint16_t raw_count() const {
    return static_cast<uint16_t>(total_count_ & kCountMask);
  }

 private:
  bool InitSpi();
  bool ConfigureAs5047p(uint16_t& initial_angle_14bit);
  bool InitEncoderTimer(uint16_t initial_abi_count);
  void DeinitSpi();
  void DeinitEncoderTimer();

  bool SpiTransferFrame(uint16_t tx_frame, uint16_t& rx_frame);
  bool ReadRegister(uint16_t addr, uint16_t& value);
  bool WriteRegister(uint16_t addr, uint16_t value);

  As5047pSensorConfig config_;
  hal_spi_handle_t hspi_{};
  hal_tim_handle_t htim_{};

  bool spi_initialized_{false};
  bool timer_initialized_{false};
  bool initialized_{false};
  Error last_error_{Error::kNone};

  uint16_t last_diagnostics_{0};
  uint16_t prev_count_{0};
  int64_t total_count_{0};

  int64_t vel_last_count_{0};
  uint32_t vel_last_cycles_{0};
};

}  // namespace stfoc
