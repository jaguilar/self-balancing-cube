#pragma once

#include <cstdint>

#include "angle_sensor.h"
#include "common/base_classes/Sensor.h"
#include "stm32_hal.h"

namespace stfoc {

struct As5048aSensorConfig {
  hal_spi_t spi_instance;
  SpiPinConfig sck;
  SpiPinConfig miso;
  SpiPinConfig mosi;
  SpiPinConfig nss;
  hal_spi_baud_rate_prescaler_t spi_prescaler;
  hal_tim_t trigger_timer;
  hal_dma_channel_t tx_dma_channel;
  hal_dma_channel_t rx_dma_channel;
  uint32_t startup_delay_ms = 10U;
  uint32_t spi_timeout_ms = 10U;
};

/// AS5048A 14-bit magnetic angle sensor driver using timer-triggered SPI DMA.
class As5048aAngleSensor final : public Sensor {
 public:
  static constexpr uint32_t kCountsPerRev = 16384U;
  static constexpr uint32_t kCountMask = kCountsPerRev - 1U;

  enum class Error : uint8_t {
    kNone = 0,
    kSpiInitFailed,
    kSpiTransferFailed,
    kDmaInitFailed,
    kParityError,
    kSensorErrorFlag,
    kCordicOverflow,
    kDiagNotReady,
    kMagnetTooStrong,
    kMagnetTooWeak,
  };

  explicit As5048aAngleSensor(const As5048aSensorConfig& config);
  ~As5048aAngleSensor();

  As5048aAngleSensor(const As5048aAngleSensor&) = delete;
  As5048aAngleSensor& operator=(const As5048aAngleSensor&) = delete;

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
  uint8_t last_agc() const {
    return static_cast<uint8_t>(last_diagnostics_ & 0xFFU);
  }
  uint16_t last_magnitude() const { return last_magnitude_; }
  int64_t total_count() const { return total_count_; }
  uint16_t raw_count() const {
    return static_cast<uint16_t>(total_count_ & kCountMask);
  }

 private:
  struct alignas(4) DmaLinkNode {
    uint32_t cbr1;
    uint32_t cllr;
  };

  bool InitSpi();
  bool ConfigureAs5048a(uint16_t& initial_angle_14bit);
  bool StartTimerTriggeredDma();
  void StopTimerTriggeredDma();
  void DeinitSpi();

  bool SpiTransferFrame(uint16_t tx_frame, uint16_t& rx_frame);
  bool ReadRegister(uint16_t addr, uint16_t& value);

  As5048aSensorConfig config_;
  hal_spi_handle_t hspi_{};

  uint16_t dma_tx_cmd_{0U};
  volatile uint16_t dma_rx_frame_{0U};
  DmaLinkNode tx_lli_node_{};
  DmaLinkNode rx_lli_node_{};

  bool spi_initialized_{false};
  bool dma_running_{false};
  bool initialized_{false};
  Error last_error_{Error::kNone};

  uint16_t last_diagnostics_{0};
  uint16_t last_magnitude_{0};
  uint16_t prev_count_{0};
  int64_t total_count_{0};

  int64_t vel_last_count_{0};
  uint32_t vel_last_cycles_{0};
};

}  // namespace stfoc
