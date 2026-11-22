#include "angle_sensor.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <optional>

#include "CppUTest/TestHarness.h"
#include "stm32_hal.h"

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
constexpr double kRadPerCount =
    kTwoPi / static_cast<double>(stfoc::As5047pAngleSensor::kCountsPerRev);

uint16_t MakeResponse(uint16_t data14, bool ef) {
  const uint16_t payload =
      static_cast<uint16_t>((ef ? (1U << 14) : 0U) | (data14 & 0x3FFFU));
  const uint16_t parity =
      static_cast<uint16_t>(__builtin_parity(payload)) << 15;
  return static_cast<uint16_t>(payload | parity);
}

class As5047pHarness {
 public:
  void Init() {
    instance_ = this;

    HAL_RCC_GPIOB_EnableClock();
    HAL_RCC_GPIOC_EnableClock();
    HAL_RCC_SPI3_EnableClock();
    CHECK_EQUAL(HAL_OK,
                HAL_RCC_SPI3_SetKernelClkSource(HAL_RCC_SPI3_CLK_SRC_PCLK1));

    // ABI outputs: PC0 (ABI_A -> PA0 enc_a), PC1 (ABI_B -> PB5 enc_b).
    hal_gpio_config_t abi_cfg{};
    abi_cfg.mode = HAL_GPIO_MODE_OUTPUT;
    abi_cfg.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
    abi_cfg.pull = HAL_GPIO_PULL_NO;
    abi_cfg.speed = HAL_GPIO_SPEED_FREQ_HIGH;
    abi_cfg.init_state = HAL_GPIO_PIN_RESET;
    CHECK_EQUAL(HAL_OK, HAL_GPIO_Init(HAL_GPIOC, HAL_GPIO_PIN_0, &abi_cfg));
    CHECK_EQUAL(HAL_OK, HAL_GPIO_Init(HAL_GPIOC, HAL_GPIO_PIN_1, &abi_cfg));
    phase_idx_ = 0U;
    DrivePhase(phase_idx_);

    // SPI3 slave pins: PC10=SCK (AF6), PC11=MISO (AF6), PB2=MOSI (AF7).
    hal_gpio_config_t spi_gpio_cfg{};
    spi_gpio_cfg.mode = HAL_GPIO_MODE_ALTERNATE;
    spi_gpio_cfg.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
    spi_gpio_cfg.pull = HAL_GPIO_PULL_DOWN;
    spi_gpio_cfg.speed = HAL_GPIO_SPEED_FREQ_HIGH;
    spi_gpio_cfg.alternate = HAL_GPIO_AF_6;
    CHECK_EQUAL(HAL_OK,
                HAL_GPIO_Init(HAL_GPIOC, HAL_GPIO_PIN_10, &spi_gpio_cfg));
    spi_gpio_cfg.pull = HAL_GPIO_PULL_NO;
    CHECK_EQUAL(HAL_OK,
                HAL_GPIO_Init(HAL_GPIOC, HAL_GPIO_PIN_11, &spi_gpio_cfg));
    spi_gpio_cfg.alternate = HAL_GPIO_AF_7;
    CHECK_EQUAL(HAL_OK,
                HAL_GPIO_Init(HAL_GPIOB, HAL_GPIO_PIN_2, &spi_gpio_cfg));

    CHECK_EQUAL(HAL_OK, HAL_SPI_Init(&hspi3_, HAL_SPI3));

    hal_spi_config_t spi_cfg{};
    spi_cfg.mode = HAL_SPI_MODE_SLAVE;
    spi_cfg.direction = HAL_SPI_DIRECTION_FULL_DUPLEX;
    spi_cfg.data_width = HAL_SPI_DATA_WIDTH_16_BIT;
    spi_cfg.clock_polarity = HAL_SPI_CLOCK_POLARITY_LOW;
    spi_cfg.clock_phase = HAL_SPI_CLOCK_PHASE_2_EDGE;
    spi_cfg.baud_rate_prescaler = HAL_SPI_BAUD_RATE_PRESCALER_2;
    spi_cfg.first_bit = HAL_SPI_MSB_FIRST;
    spi_cfg.nss_pin_management = HAL_SPI_NSS_PIN_MGMT_INTERNAL;
    CHECK_EQUAL(HAL_OK, HAL_SPI_SetConfig(&hspi3_, &spi_cfg));
    CHECK_EQUAL(HAL_OK, HAL_SPI_SetFifoThreshold(&hspi3_,
                                                 HAL_SPI_FIFO_THRESHOLD_1_DATA));

    LL_SPI_SetInternalSSLevel(SPI3, LL_SPI_SS_LEVEL_LOW);
    LL_SPI_SetTransferSize(SPI3, 0U);
    LL_SPI_ClearFlag_OVR(SPI3);
    LL_SPI_ClearFlag_UDR(SPI3);

    ResetRegisters();

    LL_SPI_Enable(SPI3);
    LL_SPI_TransmitData16(SPI3, MakeResponse(0U, false));
    LL_SPI_EnableIT_RXP(SPI3);

    HAL_CORTEX_NVIC_SetPriority(SPI3_IRQn, HAL_CORTEX_NVIC_PREEMP_PRIORITY_1,
                                HAL_CORTEX_NVIC_SUB_PRIORITY_0);
    HAL_CORTEX_NVIC_EnableIRQ(SPI3_IRQn);
    initialized_ = true;
  }

  void Deinit() {
    if (!initialized_) {
      return;
    }
    HAL_CORTEX_NVIC_DisableIRQ(SPI3_IRQn);
    LL_SPI_DisableIT_RXP(SPI3);
    LL_SPI_Disable(SPI3);
    HAL_SPI_DeInit(&hspi3_);
    HAL_RCC_SPI3_Reset();
    HAL_RCC_SPI3_DisableClock();
    HAL_GPIO_DeInit(HAL_GPIOC, HAL_GPIO_PIN_0 | HAL_GPIO_PIN_1 |
                                   HAL_GPIO_PIN_10 | HAL_GPIO_PIN_11);
    HAL_GPIO_DeInit(HAL_GPIOB, HAL_GPIO_PIN_2);
    instance_ = nullptr;
    initialized_ = false;
  }

  void ResetRegisters() {
    errfl_ = 0U;
    settings1_ = 0x0001U;  // Factory bit 0 set at reset.
    settings2_ = 0x0000U;
    diaagc_ = 0x0180U;  // LF=1, COF=0, AGC=0x80.
    anglecom_ = 0U;
    parity_errors_ = 0U;
    rx_count_ = 0U;
    udr_count_ = 0U;
    ovr_count_ = 0U;
    write_pending_ = false;
    pending_write_addr_ = 0U;
    ef_next_ = false;
    inject_bad_parity_ = false;
    inject_error_flag_ = false;
    ignore_settings1_writes_ = false;
    ignore_settings2_writes_ = false;
  }

  void SetDiaagc(uint16_t value) { diaagc_ = value; }
  void SetAngleCom(uint16_t value14) { anglecom_ = value14 & 0x3FFFU; }
  void SetInjectBadParity(bool enable) { inject_bad_parity_ = enable; }
  void SetInjectErrorFlag(bool enable) { inject_error_flag_ = enable; }
  void SetIgnoreSettings1Writes(bool enable) {
    ignore_settings1_writes_ = enable;
  }
  void SetIgnoreSettings2Writes(bool enable) {
    ignore_settings2_writes_ = enable;
  }

  uint16_t settings1() const { return settings1_; }
  uint16_t settings2() const { return settings2_; }
  uint32_t parity_errors() const { return parity_errors_; }
  uint32_t rx_count() const { return rx_count_; }
  uint32_t udr_count() const { return udr_count_; }
  uint32_t ovr_count() const { return ovr_count_; }

  void Step(int32_t delta) {
    const int32_t dir = (delta >= 0) ? 1 : -1;
    const int32_t steps = (delta >= 0) ? delta : -delta;
    for (int32_t i = 0; i < steps; ++i) {
      phase_idx_ = static_cast<uint8_t>((phase_idx_ + dir) & 0x03);
      DrivePhase(phase_idx_);
      for (int d = 0; d < 32; ++d) {
        __NOP();
      }
    }
  }

  void HandleSpi3Irq() {
    while (LL_SPI_IsActiveFlag_RXP(SPI3) != 0U) {
      const uint16_t rx = LL_SPI_ReceiveData16(SPI3);
      rx_count_ = rx_count_ + 1U;
      const uint16_t next_tx = ProcessRxFrame(rx);
      LL_SPI_TransmitData16(SPI3, next_tx);
    }
    if (LL_SPI_IsActiveFlag_OVR(SPI3) != 0U) {
      ovr_count_ = ovr_count_ + 1U;
      LL_SPI_ClearFlag_OVR(SPI3);
    }
    if (LL_SPI_IsActiveFlag_UDR(SPI3) != 0U) {
      udr_count_ = udr_count_ + 1U;
      LL_SPI_ClearFlag_UDR(SPI3);
    }
  }

  static As5047pHarness* active_instance() { return instance_; }

 private:
  static void DrivePhase(uint8_t idx) {
    struct State {
      hal_gpio_pin_state_t a;
      hal_gpio_pin_state_t b;
    };
    static constexpr std::array<State, 4> kTable = {{
        {HAL_GPIO_PIN_RESET, HAL_GPIO_PIN_RESET},
        {HAL_GPIO_PIN_SET, HAL_GPIO_PIN_RESET},
        {HAL_GPIO_PIN_SET, HAL_GPIO_PIN_SET},
        {HAL_GPIO_PIN_RESET, HAL_GPIO_PIN_SET},
    }};
    HAL_GPIO_WritePin(HAL_GPIOC, HAL_GPIO_PIN_0, kTable[idx].a);
    HAL_GPIO_WritePin(HAL_GPIOC, HAL_GPIO_PIN_1, kTable[idx].b);
  }

  uint16_t ReadReg(uint16_t addr) {
    switch (addr) {
      case 0x0000U:
        return 0U;
      case 0x0001U: {
        const uint16_t val = errfl_;
        errfl_ = 0U;
        return val;
      }
      case 0x0018U:
        return settings1_;
      case 0x0019U:
        return settings2_;
      case 0x3FFCU:
        return diaagc_;
      case 0x3FFFU:
        return anglecom_;
      default:
        errfl_ |= (1U << 1);  // INVCOMM
        ef_next_ = true;
        return 0U;
    }
  }

  uint16_t FormatResponse(uint16_t data14, bool ef) const {
    uint16_t frame = MakeResponse(data14, ef || inject_error_flag_);
    if (inject_bad_parity_) {
      frame ^= (1U << 15);
    }
    return frame;
  }

  uint16_t WriteReg(uint16_t addr, uint16_t val14) {
    switch (addr) {
      case 0x0018U: {
        const uint16_t old_val = settings1_;
        if (!ignore_settings1_writes_) {
          settings1_ = static_cast<uint16_t>(val14 & 0x00FFU);
        }
        return old_val;
      }
      case 0x0019U: {
        const uint16_t old_val = settings2_;
        if (!ignore_settings2_writes_) {
          settings2_ = static_cast<uint16_t>(val14 & 0x00FFU);
        }
        return old_val;
      }
      default:
        errfl_ |= (1U << 1);  // INVCOMM
        ef_next_ = true;
        return 0U;
    }
  }

  uint16_t ProcessRxFrame(uint16_t rx) {
    const bool ef = ef_next_;
    ef_next_ = false;

    if (__builtin_parity(rx) != 0) {
      parity_errors_ = parity_errors_ + 1U;
      errfl_ |= (1U << 2);  // PARERR
      ef_next_ = true;
      write_pending_ = false;
      return FormatResponse(0U, true);
    }

    if (write_pending_) {
      write_pending_ = false;
      const uint16_t data14 = static_cast<uint16_t>(rx & 0x3FFFU);
      const uint16_t old_val = WriteReg(pending_write_addr_, data14);
      // After write data frame, next NOP returns new value.
      (void)old_val;
      return FormatResponse(ReadReg(pending_write_addr_), ef || ef_next_);
    }

    const bool is_read = (rx & (1U << 14)) != 0U;
    const uint16_t addr = static_cast<uint16_t>(rx & 0x3FFFU);
    if (is_read) {
      const uint16_t val = ReadReg(addr);
      return FormatResponse(val, ef);
    }

    write_pending_ = true;
    pending_write_addr_ = addr;
    return FormatResponse(0U, ef);
  }

  static inline As5047pHarness* instance_{nullptr};

  hal_spi_handle_t hspi3_{};
  bool initialized_{false};
  uint8_t phase_idx_{0};

  volatile uint16_t errfl_{0};
  volatile uint16_t settings1_{0x0001U};
  volatile uint16_t settings2_{0};
  volatile uint16_t diaagc_{0x0180U};
  volatile uint16_t anglecom_{0};
  volatile uint32_t parity_errors_{0};
  volatile uint32_t rx_count_{0};
  volatile uint32_t udr_count_{0};
  volatile uint32_t ovr_count_{0};

  bool write_pending_{false};
  uint16_t pending_write_addr_{0};
  bool ef_next_{false};
  bool inject_bad_parity_{false};
  bool inject_error_flag_{false};
  bool ignore_settings1_writes_{false};
  bool ignore_settings2_writes_{false};
};

}  // namespace

extern "C" void SPI3_IRQHandler(void) {
  if (auto* h = As5047pHarness::active_instance(); h != nullptr) {
    h->HandleSpi3Irq();
  }
}

TEST_GROUP(AngleSensor) {
  As5047pHarness harness;
  std::optional<stfoc::As5047pAngleSensor> sensor;

  void setup() override {
    harness.Init();
    auto cfg = stfoc::DefaultAngleSensorConfig();
    cfg.startup_delay_ms = 0U;
    sensor.emplace(cfg);
  }

  void teardown() override {
    sensor.reset();
    harness.Deinit();
  }

  void DumpDiagIfFailed() const {
    if (!sensor->initialized()) {
      std::printf(
          "INIT FAIL err=%s (%u) diag: rx=%lu par_err=%lu udr=%lu ovr=%lu "
          "s1=0x%04X s2=0x%04X last_diag=0x%04X\r\n",
          stfoc::As5047pAngleSensor::ErrorToString(sensor->last_error()),
          static_cast<unsigned>(sensor->last_error()),
          static_cast<unsigned long>(harness.rx_count()),
          static_cast<unsigned long>(harness.parity_errors()),
          static_cast<unsigned long>(harness.udr_count()),
          static_cast<unsigned long>(harness.ovr_count()),
          static_cast<unsigned>(harness.settings1()),
          static_cast<unsigned>(harness.settings2()),
          static_cast<unsigned>(sensor->last_diagnostics()));
    }
  }

  void ExpectInitSuccess() {
    sensor->init();
    DumpDiagIfFailed();
    CHECK_TRUE(sensor->initialized());
    LONGS_EQUAL(
        static_cast<long>(stfoc::As5047pAngleSensor::Error::kNone),
        static_cast<long>(sensor->last_error()));
  }

  void ExpectInitFailure(stfoc::As5047pAngleSensor::Error expected_error) {
    sensor->init();
    if (sensor->initialized() || sensor->last_error() != expected_error) {
      std::printf(
          "UNEXPECTED INIT RESULT expected_err=%s (%u) actual_err=%s (%u) "
          "initialized=%d\r\n",
          stfoc::As5047pAngleSensor::ErrorToString(expected_error),
          static_cast<unsigned>(expected_error),
          stfoc::As5047pAngleSensor::ErrorToString(sensor->last_error()),
          static_cast<unsigned>(sensor->last_error()),
          sensor->initialized() ? 1 : 0);
      DumpDiagIfFailed();
    }
    CHECK_FALSE(sensor->initialized());
    LONGS_EQUAL(static_cast<long>(expected_error),
                static_cast<long>(sensor->last_error()));
  }
};

TEST(AngleSensor, InitConfiguresAs5047pAndSeedsInitialAngle) {
  // 1250 in 12-bit ABI corresponds to 1250 << 2 = 5000 in 14-bit ANGLECOM.
  harness.SetDiaagc(0x0180U);
  harness.SetAngleCom((1250U << 2) | 3U);

  ExpectInitSuccess();
  LONGS_EQUAL(0, harness.parity_errors());
  LONGS_EQUAL(0x0020U, harness.settings1());
  LONGS_EQUAL(0x0018U, harness.settings2());
  LONGS_EQUAL(1250, sensor->raw_count());
  LONGS_EQUAL(1250, static_cast<long>(sensor->total_count()));
  LONGS_EQUAL(0, sensor->getFullRotations());

  const double expected_rad = 1250.0 * kRadPerCount;
  DOUBLES_EQUAL(expected_rad, sensor->getSensorAngle(), 1e-5);
  DOUBLES_EQUAL(expected_rad, sensor->getMechanicalAngle(), 1e-5);
  DOUBLES_EQUAL(expected_rad, sensor->getAngle(), 1e-5);
  DOUBLES_EQUAL(expected_rad, sensor->getPreciseAngle(), 1e-9);
}

TEST(AngleSensor, ForwardAndBackwardRolloverTracking) {
  harness.SetAngleCom(4090U << 2);
  ExpectInitSuccess();
  LONGS_EQUAL(4090, sensor->raw_count());

  // Roll forward across 4095 -> 0 boundary (+10 counts -> 4100 total, raw 4).
  harness.Step(10);
  sensor->update();
  LONGS_EQUAL(4100, static_cast<long>(sensor->total_count()));
  LONGS_EQUAL(4, sensor->raw_count());
  LONGS_EQUAL(1, sensor->getFullRotations());

  // Advance another full revolution in chunks < 2048 counts.
  for (int i = 0; i < 4; ++i) {
    harness.Step(1024);
    sensor->update();
  }
  LONGS_EQUAL(8196, static_cast<long>(sensor->total_count()));
  LONGS_EQUAL(4, sensor->raw_count());
  LONGS_EQUAL(2, sensor->getFullRotations());

  // Step backward past zero into negative rotations.
  for (int i = 0; i < 9; ++i) {
    harness.Step(-1000);
    sensor->update();
  }
  // 8196 - 9000 = -804.
  LONGS_EQUAL(-804, static_cast<long>(sensor->total_count()));
  LONGS_EQUAL(4096 - 804, sensor->raw_count());
  LONGS_EQUAL(-1, sensor->getFullRotations());

  const double reconstructed =
      static_cast<double>(sensor->getFullRotations()) * kTwoPi +
      static_cast<double>(sensor->getMechanicalAngle());
  DOUBLES_EQUAL(sensor->getAngle(), reconstructed, 1e-4);
}

TEST(AngleSensor, VelocityMeasurement) {
  harness.SetAngleCom(100U << 2);
  ExpectInitSuccess();
  sensor->min_elapsed_time = 0.001f;

  // Step +400 counts over ~10 ms -> ~61.36 rad/s.
  HAL_Delay(10);
  harness.Step(400);
  sensor->update();
  const float vel_fwd = sensor->getVelocity();
  CHECK_TRUE(vel_fwd > 20.0f && vel_fwd < 150.0f);

  // Step -400 counts over ~10 ms -> ~-61.36 rad/s.
  HAL_Delay(10);
  harness.Step(-400);
  sensor->update();
  const float vel_rev = sensor->getVelocity();
  CHECK_TRUE(vel_rev < -20.0f && vel_rev > -150.0f);
}

TEST(AngleSensor, InitFailsOnCordicOverflow) {
  harness.SetDiaagc(0x0380U);  // LF=1, COF=1
  harness.SetAngleCom(0U);

  ExpectInitFailure(stfoc::As5047pAngleSensor::Error::kCordicOverflow);
  LONGS_EQUAL(0x0380U, sensor->last_diagnostics());
}

TEST(AngleSensor, InitFailsWhenDiagNotReady) {
  harness.SetDiaagc(0x0080U);  // LF=0, COF=0
  harness.SetAngleCom(0U);

  ExpectInitFailure(stfoc::As5047pAngleSensor::Error::kDiagNotReady);
  LONGS_EQUAL(0x0080U, sensor->last_diagnostics());
}

TEST(AngleSensor, InitFailsOnParityError) {
  harness.SetInjectBadParity(true);

  ExpectInitFailure(stfoc::As5047pAngleSensor::Error::kParityError);
}

TEST(AngleSensor, InitFailsOnSensorErrorFlag) {
  harness.SetInjectErrorFlag(true);

  ExpectInitFailure(stfoc::As5047pAngleSensor::Error::kSensorErrorFlag);
}

TEST(AngleSensor, InitFailsOnSettings1VerifyMismatch) {
  harness.SetIgnoreSettings1Writes(true);

  ExpectInitFailure(stfoc::As5047pAngleSensor::Error::kSettings1VerifyFailed);
}

TEST(AngleSensor, InitFailsOnSettings2VerifyMismatch) {
  harness.SetIgnoreSettings2Writes(true);

  ExpectInitFailure(stfoc::As5047pAngleSensor::Error::kSettings2VerifyFailed);
}

TEST(AngleSensor, InitFailsOnSpiTimeout) {
  auto cfg = stfoc::DefaultAngleSensorConfig();
  cfg.startup_delay_ms = 0U;
  cfg.spi_timeout_ms = 0U;
  sensor.emplace(cfg);

  ExpectInitFailure(stfoc::As5047pAngleSensor::Error::kSpiTransferFailed);
}

TEST(AngleSensor, ReinitSucceedsAfterFailure) {
  harness.SetDiaagc(0x0380U);  // LF=1, COF=1
  ExpectInitFailure(stfoc::As5047pAngleSensor::Error::kCordicOverflow);

  harness.SetDiaagc(0x0180U);  // LF=1, COF=0
  harness.SetAngleCom(500U << 2);
  ExpectInitSuccess();
  LONGS_EQUAL(500, sensor->raw_count());
}
