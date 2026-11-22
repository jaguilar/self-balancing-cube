#include "angle_sensor.h"

#include <cassert>
#include <cstddef>
#include <numbers>

namespace stfoc {
namespace {

constexpr uint16_t kRegNop = 0x0000U;
constexpr uint16_t kRegErrfl = 0x0001U;
constexpr uint16_t kRegSettings1 = 0x0018U;
constexpr uint16_t kRegSettings2 = 0x0019U;
constexpr uint16_t kRegDiaagc = 0x3FFCU;
constexpr uint16_t kRegAnglecom = 0x3FFFU;

// SETTINGS1: ABIBIN=1 (4096 CPR), DAECDIS=0 (DAEC on for min latency),
// UVW_ABI=0 (ABI active), Dataselect=0 (0x3FFF=DAECANG), PWMon=0, DIR=0, bit0=0.
constexpr uint16_t kSettings1Value = 0x0020U;
constexpr uint16_t kSettings1VerifyMask = 0x00FEU;

// SETTINGS2: ABIRES=000 (max resolution 4096 CPR), HYS=11 (0 LSB, min lag),
// UVWPP=000.
constexpr uint16_t kSettings2Value = 0x0018U;
constexpr uint16_t kSettings2VerifyMask = 0x00FFU;

constexpr uint16_t kDiaagcLfMask = 1U << 8;
constexpr uint16_t kDiaagcCofMask = 1U << 9;
constexpr uint32_t kDiagPollAttempts = 20U;

constexpr uint16_t kCmdReadBit = 1U << 14;
constexpr uint16_t kRespErrorFlagBit = 1U << 14;
constexpr uint16_t kDataMask14 = 0x3FFFU;

constexpr float kTwoPiFloat = 2.0f * std::numbers::pi_v<float>;
constexpr double kTwoPiDouble = 2.0 * std::numbers::pi_v<double>;
constexpr float kRadPerCountFloat =
    kTwoPiFloat / static_cast<float>(As5047pAngleSensor::kCountsPerRev);
constexpr double kRadPerCountDouble =
    kTwoPiDouble / static_cast<double>(As5047pAngleSensor::kCountsPerRev);

uint16_t WithEvenParity(uint16_t lower15) {
  const uint16_t payload = static_cast<uint16_t>(lower15 & 0x7FFFU);
  const uint16_t parity =
      static_cast<uint16_t>(__builtin_parity(payload)) << 15;
  return static_cast<uint16_t>(payload | parity);
}

bool HasValidEvenParity(uint16_t frame) {
  return __builtin_parity(frame) == 0;
}

uint16_t MakeReadCmd(uint16_t addr) {
  return WithEvenParity(static_cast<uint16_t>(kCmdReadBit | (addr & kDataMask14)));
}

uint16_t MakeWriteCmd(uint16_t addr) {
  return WithEvenParity(static_cast<uint16_t>(addr & kDataMask14));
}

uint16_t MakeWriteData(uint16_t data) {
  return WithEvenParity(static_cast<uint16_t>(data & kDataMask14));
}

constexpr uint16_t kReadNopCmd = 0xC000U;  // Even parity, R/W=1, addr=0x0000.

void EnableGpioClock(hal_gpio_t port) {
  switch (port) {
    case HAL_GPIOA:
      HAL_RCC_GPIOA_EnableClock();
      break;
    case HAL_GPIOB:
      HAL_RCC_GPIOB_EnableClock();
      break;
    case HAL_GPIOC:
      HAL_RCC_GPIOC_EnableClock();
      break;
    case HAL_GPIOD:
      HAL_RCC_GPIOD_EnableClock();
      break;
    case HAL_GPIOE:
      HAL_RCC_GPIOE_EnableClock();
      break;
#if defined(GPIOF)
    case HAL_GPIOF:
      HAL_RCC_GPIOF_EnableClock();
      break;
#endif
#if defined(GPIOG)
    case HAL_GPIOG:
      HAL_RCC_GPIOG_EnableClock();
      break;
#endif
    case HAL_GPIOH:
      HAL_RCC_GPIOH_EnableClock();
      break;
    default:
      assert(false && "Unsupported GPIO port");
      break;
  }
}

bool EnableSpiClock(hal_spi_t instance) {
  switch (instance) {
    case HAL_SPI1:
      HAL_RCC_SPI1_EnableClock();
      return HAL_RCC_SPI1_SetKernelClkSource(HAL_RCC_SPI1_CLK_SRC_PCLK2) ==
             HAL_OK;
    case HAL_SPI2:
      HAL_RCC_SPI2_EnableClock();
      return HAL_RCC_SPI2_SetKernelClkSource(HAL_RCC_SPI2_CLK_SRC_PCLK1) ==
             HAL_OK;
#if defined(SPI3)
    case HAL_SPI3:
      HAL_RCC_SPI3_EnableClock();
      return HAL_RCC_SPI3_SetKernelClkSource(HAL_RCC_SPI3_CLK_SRC_PCLK1) ==
             HAL_OK;
#endif
    default:
      assert(false && "Unsupported SPI instance");
      return false;
  }
}

void EnableTimerClock(hal_tim_t instance) {
  switch (instance) {
    case HAL_TIM2:
      HAL_RCC_TIM2_EnableClock();
      break;
#if defined(TIM3)
    case HAL_TIM3:
      HAL_RCC_TIM3_EnableClock();
      break;
#endif
#if defined(TIM4)
    case HAL_TIM4:
      HAL_RCC_TIM4_EnableClock();
      break;
#endif
#if defined(TIM5)
    case HAL_TIM5:
      HAL_RCC_TIM5_EnableClock();
      break;
#endif
    default:
      assert(false && "Unsupported encoder timer instance");
      break;
  }
}

void EnableDwtCycleCounter() {
  DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

}  // namespace

As5047pAngleSensor::As5047pAngleSensor(const As5047pSensorConfig& config)
    : config_(config) {}

As5047pAngleSensor::~As5047pAngleSensor() { deinit(); }

const char* As5047pAngleSensor::ErrorToString(Error error) {
  switch (error) {
    case Error::kNone:
      return "kNone";
    case Error::kSpiInitFailed:
      return "kSpiInitFailed";
    case Error::kSpiTransferFailed:
      return "kSpiTransferFailed";
    case Error::kParityError:
      return "kParityError";
    case Error::kSensorErrorFlag:
      return "kSensorErrorFlag";
    case Error::kSettings1VerifyFailed:
      return "kSettings1VerifyFailed";
    case Error::kSettings2VerifyFailed:
      return "kSettings2VerifyFailed";
    case Error::kCordicOverflow:
      return "kCordicOverflow";
    case Error::kDiagNotReady:
      return "kDiagNotReady";
    case Error::kTimerInitFailed:
      return "kTimerInitFailed";
  }
  return "Unknown";
}

void As5047pAngleSensor::init() {
  if (initialized_ || spi_initialized_ || timer_initialized_) {
    deinit();
  }
  last_error_ = Error::kNone;
  last_diagnostics_ = 0U;

  EnableDwtCycleCounter();

  if (!InitSpi()) {
    deinit();
    return;
  }

  if (config_.startup_delay_ms > 0U) {
    HAL_Delay(config_.startup_delay_ms);
  }

  uint16_t initial_angle_14bit = 0U;
  if (!ConfigureAs5047p(initial_angle_14bit)) {
    deinit();
    return;
  }

  const uint16_t initial_abi =
      static_cast<uint16_t>((initial_angle_14bit >> 2) & kCountMask);
  if (!InitEncoderTimer(initial_abi)) {
    deinit();
    return;
  }

  prev_count_ = initial_abi;
  total_count_ = initial_abi;
  vel_last_count_ = initial_abi;
  vel_last_cycles_ = DWT->CYCCNT;
  velocity = 0.0f;
  initialized_ = true;
}

void As5047pAngleSensor::deinit() {
  DeinitEncoderTimer();
  DeinitSpi();
  initialized_ = false;
}

void As5047pAngleSensor::update() {
  if (!initialized_) {
    return;
  }

  const uint16_t current_count =
      static_cast<uint16_t>(HAL_TIM_GetCounter(&htim_) & kCountMask);
  const uint16_t raw_diff =
      static_cast<uint16_t>((current_count - prev_count_) & kCountMask);
  const int16_t signed_diff = static_cast<int16_t>(raw_diff << 4) >> 4;

  total_count_ += signed_diff;
  prev_count_ = current_count;
}

float As5047pAngleSensor::getSensorAngle() {
  return static_cast<float>(total_count_ & kCountMask) * kRadPerCountFloat;
}

float As5047pAngleSensor::getMechanicalAngle() {
  return static_cast<float>(total_count_ & kCountMask) * kRadPerCountFloat;
}

float As5047pAngleSensor::getAngle() {
  return static_cast<float>(total_count_) * kRadPerCountFloat;
}

double As5047pAngleSensor::getPreciseAngle() {
  return static_cast<double>(total_count_) * kRadPerCountDouble;
}

int32_t As5047pAngleSensor::getFullRotations() {
  return static_cast<int32_t>(total_count_ >> 12);
}

float As5047pAngleSensor::getVelocity() {
  if (!initialized_) {
    return 0.0f;
  }

  const uint32_t now_cycles = DWT->CYCCNT;
  const float dt = static_cast<float>(now_cycles - vel_last_cycles_) /
                   static_cast<float>(SystemCoreClock);
  if (dt < min_elapsed_time || dt <= 0.0f) {
    return velocity;
  }

  const int64_t count_diff = total_count_ - vel_last_count_;
  velocity = static_cast<float>(count_diff) * kRadPerCountFloat / dt;
  vel_last_count_ = total_count_;
  vel_last_cycles_ = now_cycles;
  return velocity;
}

bool As5047pAngleSensor::InitSpi() {
  if (!EnableSpiClock(config_.spi_instance)) {
    last_error_ = Error::kSpiInitFailed;
    return false;
  }

  if (HAL_SPI_Init(&hspi_, config_.spi_instance) != HAL_OK) {
    last_error_ = Error::kSpiInitFailed;
    return false;
  }
  spi_initialized_ = true;

  hal_spi_config_t spi_cfg{};
  spi_cfg.mode = HAL_SPI_MODE_MASTER;
  spi_cfg.direction = HAL_SPI_DIRECTION_FULL_DUPLEX;
  spi_cfg.data_width = HAL_SPI_DATA_WIDTH_16_BIT;
  spi_cfg.clock_polarity = HAL_SPI_CLOCK_POLARITY_LOW;
  spi_cfg.clock_phase = HAL_SPI_CLOCK_PHASE_2_EDGE;
  spi_cfg.baud_rate_prescaler = config_.spi_prescaler;
  spi_cfg.first_bit = HAL_SPI_MSB_FIRST;
  spi_cfg.nss_pin_management = HAL_SPI_NSS_PIN_MGMT_OUTPUT;
  if (HAL_SPI_SetConfig(&hspi_, &spi_cfg) != HAL_OK) {
    last_error_ = Error::kSpiInitFailed;
    return false;
  }

  // AS5047P requires t_L >= 350 ns (NSS falling -> first SCK edge) and
  // t_CSn >= 350 ns (NSS high between frames). At 9 MHz (111 ns/cycle),
  // 4 cycles = 444 ns.
  hal_spi_nss_config_t nss_cfg{};
  nss_cfg.nss_pulse = HAL_SPI_NSS_PULSE_ENABLE;
  nss_cfg.nss_polarity = HAL_SPI_NSS_POLARITY_LOW;
  nss_cfg.nss_mssi_delay = HAL_SPI_NSS_MSSI_DELAY_4_CYCLE;
  if (HAL_SPI_SetConfigNSS(&hspi_, &nss_cfg) != HAL_OK ||
      HAL_SPI_MASTER_SetInterDataIdlenessDelay(&hspi_,
                                               HAL_SPI_MIDI_DELAY_4_CYCLE) !=
          HAL_OK ||
      HAL_SPI_MASTER_EnableKeepIOState(&hspi_) != HAL_OK) {
    last_error_ = Error::kSpiInitFailed;
    return false;
  }

  const SpiPinConfig pins[] = {
      config_.sck,
      config_.miso,
      config_.mosi,
      config_.nss,
  };
  for (const auto& p : pins) {
    EnableGpioClock(p.port);
    hal_gpio_config_t gpio_cfg{};
    gpio_cfg.mode = HAL_GPIO_MODE_ALTERNATE;
    gpio_cfg.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
    gpio_cfg.pull = HAL_GPIO_PULL_NO;
    gpio_cfg.speed = HAL_GPIO_SPEED_FREQ_HIGH;
    gpio_cfg.alternate = p.alternate;
    if (HAL_GPIO_Init(p.port, p.pin, &gpio_cfg) != HAL_OK) {
      last_error_ = Error::kSpiInitFailed;
      return false;
    }
  }

  return true;
}

void As5047pAngleSensor::DeinitSpi() {
  if (!spi_initialized_) {
    return;
  }
  HAL_SPI_DeInit(&hspi_);
  HAL_GPIO_DeInit(config_.sck.port, config_.sck.pin);
  HAL_GPIO_DeInit(config_.miso.port, config_.miso.pin);
  HAL_GPIO_DeInit(config_.mosi.port, config_.mosi.pin);
  HAL_GPIO_DeInit(config_.nss.port, config_.nss.pin);
  spi_initialized_ = false;
}

bool As5047pAngleSensor::SpiTransferFrame(uint16_t tx_frame,
                                          uint16_t& rx_frame) {
  uint16_t rx = 0U;
  if (HAL_SPI_TransmitReceive(&hspi_, &tx_frame, &rx, 1U,
                              config_.spi_timeout_ms) != HAL_OK) {
    last_error_ = Error::kSpiTransferFailed;
    return false;
  }
  rx_frame = rx;
  return true;
}

bool As5047pAngleSensor::ReadRegister(uint16_t addr, uint16_t& value) {
  uint16_t rx_discard = 0U;
  uint16_t rx_resp = 0U;
  if (!SpiTransferFrame(MakeReadCmd(addr), rx_discard)) {
    return false;
  }
  if (!SpiTransferFrame(kReadNopCmd, rx_resp)) {
    return false;
  }
  if (!HasValidEvenParity(rx_resp)) {
    last_error_ = Error::kParityError;
    return false;
  }
  if ((rx_resp & kRespErrorFlagBit) != 0U) {
    last_error_ = Error::kSensorErrorFlag;
    return false;
  }
  value = static_cast<uint16_t>(rx_resp & kDataMask14);
  return true;
}

bool As5047pAngleSensor::WriteRegister(uint16_t addr, uint16_t value) {
  uint16_t rx_discard = 0U;
  uint16_t rx_old = 0U;
  uint16_t rx_new = 0U;
  if (!SpiTransferFrame(MakeWriteCmd(addr), rx_discard)) {
    return false;
  }
  if (!SpiTransferFrame(MakeWriteData(value), rx_old)) {
    return false;
  }
  if (!SpiTransferFrame(kReadNopCmd, rx_new)) {
    return false;
  }
  if (!HasValidEvenParity(rx_new)) {
    last_error_ = Error::kParityError;
    return false;
  }
  if ((rx_new & kRespErrorFlagBit) != 0U) {
    last_error_ = Error::kSensorErrorFlag;
    return false;
  }
  return true;
}

bool As5047pAngleSensor::ConfigureAs5047p(uint16_t& initial_angle_14bit) {
  // Clear any startup/previous error flags in ERRFL.
  uint16_t errfl = 0U;
  (void)ReadRegister(kRegErrfl, errfl);
  last_error_ = Error::kNone;

  if (!WriteRegister(kRegSettings1, kSettings1Value)) {
    return false;
  }
  uint16_t s1_readback = 0U;
  if (!ReadRegister(kRegSettings1, s1_readback)) {
    return false;
  }
  if ((s1_readback & kSettings1VerifyMask) != kSettings1Value) {
    last_error_ = Error::kSettings1VerifyFailed;
    return false;
  }

  if (!WriteRegister(kRegSettings2, kSettings2Value)) {
    return false;
  }
  uint16_t s2_readback = 0U;
  if (!ReadRegister(kRegSettings2, s2_readback)) {
    return false;
  }
  if ((s2_readback & kSettings2VerifyMask) != kSettings2Value) {
    last_error_ = Error::kSettings2VerifyFailed;
    return false;
  }

  bool diag_ok = false;
  for (uint32_t attempt = 0U; attempt < kDiagPollAttempts; ++attempt) {
    uint16_t diag = 0U;
    if (!ReadRegister(kRegDiaagc, diag)) {
      return false;
    }
    last_diagnostics_ = diag;
    const bool lf = (diag & kDiaagcLfMask) != 0U;
    const bool cof = (diag & kDiaagcCofMask) != 0U;
    if (cof) {
      last_error_ = Error::kCordicOverflow;
      return false;
    }
    if (lf) {
      diag_ok = true;
      break;
    }
    HAL_Delay(1);
  }
  if (!diag_ok) {
    last_error_ = Error::kDiagNotReady;
    return false;
  }

  return ReadRegister(kRegAnglecom, initial_angle_14bit);
}

bool As5047pAngleSensor::InitEncoderTimer(uint16_t initial_abi_count) {
  EnableTimerClock(config_.timer_instance);

  const EncoderChannelConfig channels[] = {config_.enc_a, config_.enc_b};
  for (const auto& ch : channels) {
    EnableGpioClock(ch.port);
    hal_gpio_config_t gpio_cfg{};
    gpio_cfg.mode = HAL_GPIO_MODE_ALTERNATE;
    gpio_cfg.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
    gpio_cfg.pull = HAL_GPIO_PULL_NO;
    gpio_cfg.speed = HAL_GPIO_SPEED_FREQ_HIGH;
    gpio_cfg.alternate = ch.alternate;
    if (HAL_GPIO_Init(ch.port, ch.pin, &gpio_cfg) != HAL_OK) {
      last_error_ = Error::kTimerInitFailed;
      return false;
    }
  }

  if (HAL_TIM_Init(&htim_, config_.timer_instance) != HAL_OK) {
    last_error_ = Error::kTimerInitFailed;
    return false;
  }
  timer_initialized_ = true;

  hal_tim_config_t tim_cfg{};
  tim_cfg.prescaler = 0U;
  tim_cfg.counter_mode = HAL_TIM_COUNTER_UP;
  tim_cfg.period = kCountsPerRev - 1U;
  tim_cfg.repetition_counter = 0U;
  tim_cfg.clock_sel.clock_source = HAL_TIM_CLK_ENCODER_X4_TI12;
  if (HAL_TIM_SetConfig(&htim_, &tim_cfg) != HAL_OK ||
      HAL_TIM_SetDTSPrescaler(&htim_, HAL_TIM_DTS_DIV1) != HAL_OK ||
      HAL_TIM_SetDTS2Prescaler(&htim_, HAL_TIM_DTS2_DIV1) != HAL_OK) {
    last_error_ = Error::kTimerInitFailed;
    return false;
  }

  for (const auto& ch : channels) {
    hal_tim_ic_channel_config_t ic_ch_cfg{};
    ic_ch_cfg.source = ch.input_source;
    ic_ch_cfg.polarity = HAL_TIM_IC_RISING;
    ic_ch_cfg.filter = HAL_TIM_FDIV1;

    hal_tim_ic_capture_unit_config_t ic_unit_cfg{};
    ic_unit_cfg.source = HAL_TIM_IC_DIRECT;
    ic_unit_cfg.prescaler = HAL_TIM_IC_DIV1;

    if (HAL_TIM_IC_SetConfigChannel(&htim_, ch.channel, &ic_ch_cfg) != HAL_OK ||
        HAL_TIM_IC_SetConfigCaptureUnit(&htim_, ch.capture_unit,
                                        &ic_unit_cfg) != HAL_OK) {
      last_error_ = Error::kTimerInitFailed;
      return false;
    }
  }

  if (HAL_TIM_SetCounter(&htim_, initial_abi_count) != HAL_OK ||
      HAL_TIM_IC_StartChannel(&htim_, config_.enc_a.channel) != HAL_OK ||
      HAL_TIM_IC_StartChannel(&htim_, config_.enc_b.channel) != HAL_OK ||
      HAL_TIM_Start(&htim_) != HAL_OK) {
    last_error_ = Error::kTimerInitFailed;
    return false;
  }

  return true;
}

void As5047pAngleSensor::DeinitEncoderTimer() {
  if (!timer_initialized_) {
    return;
  }
  (void)HAL_TIM_IC_StopChannel(&htim_, config_.enc_a.channel);
  (void)HAL_TIM_IC_StopChannel(&htim_, config_.enc_b.channel);
  (void)HAL_TIM_Stop(&htim_);
  (void)HAL_TIM_DeInit(&htim_);
  HAL_GPIO_DeInit(config_.enc_a.port, config_.enc_a.pin);
  HAL_GPIO_DeInit(config_.enc_b.port, config_.enc_b.pin);
  timer_initialized_ = false;
}

}  // namespace stfoc
