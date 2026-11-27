#include "as5048a_angle_sensor.h"

#include <cassert>
#include <cstddef>
#include <numbers>

#include "stm32c5xx_ll_dma.h"
#include "stm32c5xx_ll_spi.h"
#include "stm32c5xx_ll_tim.h"

namespace stfoc {
namespace {

constexpr uint16_t kRegNop = 0x0000U;
constexpr uint16_t kRegClearErrorFlag = 0x0001U;
constexpr uint16_t kRegDiagAgc = 0x3FFDU;
constexpr uint16_t kRegMagnitude = 0x3FFEU;
constexpr uint16_t kRegAngle = 0x3FFFU;

// Diagnostics + AGC register (0x3FFD) bit masks (AS5048A datasheet Fig. 22).
constexpr uint16_t kDiagOcfMask = 1U << 8;       // Offset Compensation Finished
constexpr uint16_t kDiagCofMask = 1U << 9;       // CORDIC Overflow
constexpr uint16_t kDiagCompLowMask = 1U << 10;  // High magnetic field
constexpr uint16_t kDiagCompHighMask = 1U << 11; // Weak magnetic field
constexpr uint32_t kDiagPollAttempts = 20U;

constexpr uint16_t kCmdReadBit = 1U << 14;
constexpr uint16_t kRespErrorFlagBit = 1U << 14;
constexpr uint16_t kDataMask14 = 0x3FFFU;

constexpr float kTwoPiFloat = 2.0f * std::numbers::pi_v<float>;
constexpr double kTwoPiDouble = 2.0 * std::numbers::pi_v<double>;
constexpr float kRadPerCountFloat =
    kTwoPiFloat / static_cast<float>(As5048aAngleSensor::kCountsPerRev);
constexpr double kRadPerCountDouble =
    kTwoPiDouble / static_cast<double>(As5048aAngleSensor::kCountsPerRev);

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
  return WithEvenParity(
      static_cast<uint16_t>(kCmdReadBit | (addr & kDataMask14)));
}

// NOP command (0x0000): even parity, R/W=0, addr=0x0000 (datasheet Fig. 28).
constexpr uint16_t kNopCmd = kRegNop;

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

void EnableDwtCycleCounter() {
  DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

bool EnableDmaClock(hal_dma_channel_t channel) {
  const uintptr_t addr = static_cast<uintptr_t>(channel);
  if (addr >= LPDMA1_CH0_BASE && addr <= LPDMA1_CH7_BASE) {
    HAL_RCC_LPDMA1_EnableClock();
    return true;
  }
#if defined(LPDMA2)
#if defined(LPDMA2_CH7_BASE)
  constexpr uintptr_t kLpdma2LastChBase = LPDMA2_CH7_BASE;
#else
  constexpr uintptr_t kLpdma2LastChBase = LPDMA2_CH3_BASE;
#endif
  if (addr >= LPDMA2_CH0_BASE && addr <= kLpdma2LastChBase) {
    HAL_RCC_LPDMA2_EnableClock();
    return true;
  }
#endif
  return false;
}

bool MapTimerUpdateDmaRequest(hal_tim_t timer, uint32_t& req) {
  switch (timer) {
    case HAL_TIM1:
      req = LL_LPDMA1_REQUEST_TIM1_UPD;
      return true;
    case HAL_TIM6:
      req = LL_LPDMA1_REQUEST_TIM6_UPD;
      return true;
    case HAL_TIM7:
      req = LL_LPDMA1_REQUEST_TIM7_UPD;
      return true;
    case HAL_TIM15:
      req = LL_LPDMA1_REQUEST_TIM15_UPD;
      return true;
#if defined(TIM16)
    case HAL_TIM16:
      req = LL_LPDMA1_REQUEST_TIM16_UPD;
      return true;
#endif
#if defined(TIM17)
    case HAL_TIM17:
      req = LL_LPDMA1_REQUEST_TIM17_UPD;
      return true;
#endif
    default:
      return false;
  }
}

bool MapSpiRxDmaRequest(hal_spi_t spi, uint32_t& req) {
  switch (spi) {
    case HAL_SPI1:
      req = LL_LPDMA1_REQUEST_SPI1_RX;
      return true;
    case HAL_SPI2:
      req = LL_LPDMA1_REQUEST_SPI2_RX;
      return true;
#if defined(SPI3)
    case HAL_SPI3:
      req = LL_LPDMA1_REQUEST_SPI3_RX;
      return true;
#endif
    default:
      return false;
  }
}

void ResetDmaChannel(DMA_Channel_TypeDef* ch) {
  if (LL_DMA_IsEnabledChannel(ch) != 0U) {
    LL_DMA_SuspendChannel(ch);
    while (LL_DMA_IsActiveFlag_SUSP(ch) == 0U) {
    }
  }
  LL_DMA_ResetChannel(ch);
}

}  // namespace

As5048aAngleSensor::As5048aAngleSensor(const As5048aSensorConfig& config)
    : config_(config) {}

As5048aAngleSensor::~As5048aAngleSensor() { Deinit(); }

const char* As5048aAngleSensor::ErrorToString(Error error) {
  switch (error) {
    case Error::kNone:
      return "kNone";
    case Error::kSpiInitFailed:
      return "kSpiInitFailed";
    case Error::kSpiTransferFailed:
      return "kSpiTransferFailed";
    case Error::kDmaInitFailed:
      return "kDmaInitFailed";
    case Error::kParityError:
      return "kParityError";
    case Error::kSensorErrorFlag:
      return "kSensorErrorFlag";
    case Error::kCordicOverflow:
      return "kCordicOverflow";
    case Error::kDiagNotReady:
      return "kDiagNotReady";
    case Error::kMagnetTooStrong:
      return "kMagnetTooStrong";
    case Error::kMagnetTooWeak:
      return "kMagnetTooWeak";
  }
  return "Unknown";
}

void As5048aAngleSensor::init() {
  if (initialized_ || spi_initialized_) {
    Deinit();
  }
  last_error_ = Error::kNone;
  last_diagnostics_ = 0U;
  last_magnitude_ = 0U;

  EnableDwtCycleCounter();

  if (!InitSpi()) {
    Deinit();
    return;
  }

  if (config_.startup_delay_ms > 0U) {
    HAL_Delay(config_.startup_delay_ms);
  }

  uint16_t initial_angle_14bit = 0U;
  if (!ConfigureAs5048a(initial_angle_14bit)) {
    Deinit();
    return;
  }

  // Prime the AS5048A command pipeline with a READ ANGLE frame so the first
  // timer-triggered transfer immediately returns a valid angle response.
  dma_tx_cmd_ = MakeReadCmd(kRegAngle);
  uint16_t prime_rx = 0U;
  if (!SpiTransferFrame(dma_tx_cmd_, prime_rx)) {
    Deinit();
    return;
  }
  dma_rx_frame_ = WithEvenParity(initial_angle_14bit);

  if (!StartTimerTriggeredDma()) {
    Deinit();
    return;
  }

  prev_count_ = initial_angle_14bit;
  total_count_ = initial_angle_14bit;
  vel_last_count_ = initial_angle_14bit;
  vel_last_cycles_ = DWT->CYCCNT;
  velocity = 0.0f;
  initialized_ = true;
}

void As5048aAngleSensor::Deinit() {
  StopTimerTriggeredDma();
  DeinitSpi();
  initialized_ = false;
}

void As5048aAngleSensor::update() {
  if (!initialized_) {
    return;
  }

  const uint16_t rx_resp = dma_rx_frame_;
  if (!HasValidEvenParity(rx_resp)) {
    last_error_ = Error::kParityError;
    return;
  }
  if ((rx_resp & kRespErrorFlagBit) != 0U) {
    last_error_ = Error::kSensorErrorFlag;
    return;
  }

  last_error_ = Error::kNone;
  const uint16_t current_count = static_cast<uint16_t>(rx_resp & kDataMask14);

  const uint16_t raw_diff =
      static_cast<uint16_t>((current_count - prev_count_) & kCountMask);
  const int16_t signed_diff = static_cast<int16_t>(raw_diff << 2) >> 2;

  total_count_ += signed_diff;
  prev_count_ = current_count;
}

float As5048aAngleSensor::getSensorAngle() {
  return static_cast<float>(total_count_ & kCountMask) * kRadPerCountFloat;
}

float As5048aAngleSensor::getMechanicalAngle() {
  return static_cast<float>(total_count_ & kCountMask) * kRadPerCountFloat;
}

float As5048aAngleSensor::getAngle() {
  return static_cast<float>(total_count_) * kRadPerCountFloat;
}

double As5048aAngleSensor::getPreciseAngle() {
  return static_cast<double>(total_count_) * kRadPerCountDouble;
}

int32_t As5048aAngleSensor::getFullRotations() {
  return static_cast<int32_t>(total_count_ >> 14);
}

float As5048aAngleSensor::getVelocity() {
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

bool As5048aAngleSensor::InitSpi() {
  if (!EnableSpiClock(config_.spi_instance)) {
    last_error_ = Error::kSpiInitFailed;
    return false;
  }

  if (HAL_SPI_Init(&hspi_, config_.spi_instance) != HAL_OK) {
    last_error_ = Error::kSpiInitFailed;
    return false;
  }
  spi_initialized_ = true;

  // AS5048A uses SPI Mode 1 (CPOL=0, CPHA=1): MOSI sampled on falling CLK edge,
  // MISO shifted out on rising CLK edge (datasheet p. 10, Fig. 14).
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

  // AS5048A requires t_L >= 350 ns (CSn falling -> first CLK rising edge) and
  // t_CSnH >= 350 ns (CSn high between frames). At 9 MHz (111 ns/cycle),
  // 4 cycles = 444 ns (minimum valid delay for maximum responsiveness).
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

void As5048aAngleSensor::DeinitSpi() {
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

bool As5048aAngleSensor::StartTimerTriggeredDma() {
  uint32_t tim_upd_req = 0U;
  uint32_t spi_rx_req = 0U;
  if (!EnableDmaClock(config_.tx_dma_channel) ||
      !EnableDmaClock(config_.rx_dma_channel) ||
      !MapTimerUpdateDmaRequest(config_.trigger_timer, tim_upd_req) ||
      !MapSpiRxDmaRequest(config_.spi_instance, spi_rx_req)) {
    last_error_ = Error::kDmaInitFailed;
    return false;
  }

  auto* spix = reinterpret_cast<SPI_TypeDef*>(
      static_cast<uintptr_t>(config_.spi_instance));
  auto* timx = reinterpret_cast<TIM_TypeDef*>(
      static_cast<uintptr_t>(config_.trigger_timer));
  auto* tx_ch = reinterpret_cast<DMA_Channel_TypeDef*>(
      static_cast<uintptr_t>(config_.tx_dma_channel));
  auto* rx_ch = reinterpret_cast<DMA_Channel_TypeDef*>(
      static_cast<uintptr_t>(config_.rx_dma_channel));

  ResetDmaChannel(tx_ch);
  ResetDmaChannel(rx_ch);

  // Configure circular 1-node LLI for RX: SPIx->RXDR -> dma_rx_frame_ on
  // SPIx_RX hardware request.
  const uintptr_t rx_node_addr = reinterpret_cast<uintptr_t>(&rx_lli_node_);
  rx_lli_node_.cbr1 = sizeof(uint16_t);
  rx_lli_node_.cllr = LL_DMA_UPDATE_CBR1 | LL_DMA_UPDATE_CLLR |
                      (static_cast<uint32_t>(rx_node_addr) & DMA_CLLR_LA);

  LL_DMA_SetSrcDataWidth(rx_ch, LL_DMA_SRC_DATA_WIDTH_HALFWORD);
  LL_DMA_SetDestDataWidth(rx_ch, LL_DMA_DEST_DATA_WIDTH_HALFWORD);
  LL_DMA_SetSrcIncMode(rx_ch, LL_DMA_SRC_ADDR_FIXED);
  LL_DMA_SetDestIncMode(rx_ch, LL_DMA_DEST_ADDR_FIXED);
  LL_DMA_SetDataTransferDirection(rx_ch, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
  LL_DMA_SetPeriphRequest(rx_ch, spi_rx_req);
  LL_DMA_SetBlkDataLength(rx_ch, sizeof(uint16_t));
  LL_DMA_ConfigAddresses(rx_ch, reinterpret_cast<uint32_t>(&spix->RXDR),
                         reinterpret_cast<uint32_t>(&dma_rx_frame_));
  LL_DMA_SetLinkedListBaseAddr(rx_ch, static_cast<uint32_t>(rx_node_addr));
  LL_DMA_ConfigLinkUpdate(rx_ch, LL_DMA_UPDATE_CBR1 | LL_DMA_UPDATE_CLLR,
                          static_cast<uint32_t>(rx_node_addr));
  LL_DMA_EnableChannel(rx_ch);

  // Configure circular 1-node LLI for TX: dma_tx_cmd_ -> SPIx->TXDR on
  // TIMx_UPD hardware request.
  const uintptr_t tx_node_addr = reinterpret_cast<uintptr_t>(&tx_lli_node_);
  tx_lli_node_.cbr1 = sizeof(uint16_t);
  tx_lli_node_.cllr = LL_DMA_UPDATE_CBR1 | LL_DMA_UPDATE_CLLR |
                      (static_cast<uint32_t>(tx_node_addr) & DMA_CLLR_LA);

  LL_DMA_SetSrcDataWidth(tx_ch, LL_DMA_SRC_DATA_WIDTH_HALFWORD);
  LL_DMA_SetDestDataWidth(tx_ch, LL_DMA_DEST_DATA_WIDTH_HALFWORD);
  LL_DMA_SetSrcIncMode(tx_ch, LL_DMA_SRC_ADDR_FIXED);
  LL_DMA_SetDestIncMode(tx_ch, LL_DMA_DEST_ADDR_FIXED);
  LL_DMA_SetDataTransferDirection(tx_ch, LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
  LL_DMA_SetPeriphRequest(tx_ch, tim_upd_req);
  LL_DMA_SetBlkDataLength(tx_ch, sizeof(uint16_t));
  LL_DMA_ConfigAddresses(tx_ch, reinterpret_cast<uint32_t>(&dma_tx_cmd_),
                         reinterpret_cast<uint32_t>(&spix->TXDR));
  LL_DMA_SetLinkedListBaseAddr(tx_ch, static_cast<uint32_t>(tx_node_addr));
  LL_DMA_ConfigLinkUpdate(tx_ch, LL_DMA_UPDATE_CBR1 | LL_DMA_UPDATE_CLLR,
                          static_cast<uint32_t>(tx_node_addr));
  LL_DMA_EnableChannel(tx_ch);

  // Put SPI into endless master full-duplex mode (TSIZE = 0) with RX DMA
  // enabled. Each 16-bit write to TXDR from the TIMx_UPD DMA channel clocks
  // out one full-duplex frame with hardware NSS pulsing.
  LL_SPI_Disable(spix);
  LL_SPI_SetFIFOThreshold(spix, LL_SPI_FIFO_THRESHOLD_1_DATA);
  LL_SPI_SetTransferSize(spix, 0U);
  LL_SPI_DisableDMAReq_TX(spix);
  LL_SPI_EnableDMAReq_RX(spix);
  LL_SPI_Enable(spix);
  LL_SPI_StartMasterTransfer(spix);

  LL_TIM_EnableDMAReq_UPDATE(timx);
  dma_running_ = true;
  return true;
}

void As5048aAngleSensor::StopTimerTriggeredDma() {
  if (!dma_running_) {
    return;
  }

  auto* spix = reinterpret_cast<SPI_TypeDef*>(
      static_cast<uintptr_t>(config_.spi_instance));
  auto* timx = reinterpret_cast<TIM_TypeDef*>(
      static_cast<uintptr_t>(config_.trigger_timer));
  auto* tx_ch = reinterpret_cast<DMA_Channel_TypeDef*>(
      static_cast<uintptr_t>(config_.tx_dma_channel));
  auto* rx_ch = reinterpret_cast<DMA_Channel_TypeDef*>(
      static_cast<uintptr_t>(config_.rx_dma_channel));

  LL_TIM_DisableDMAReq_UPDATE(timx);
  ResetDmaChannel(tx_ch);
  ResetDmaChannel(rx_ch);
  LL_SPI_DisableDMAReq_RX(spix);
  LL_SPI_Disable(spix);
  dma_running_ = false;
}

bool As5048aAngleSensor::SpiTransferFrame(uint16_t tx_frame,
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

bool As5048aAngleSensor::ReadRegister(uint16_t addr, uint16_t& value) {
  uint16_t rx_discard = 0U;
  uint16_t rx_resp = 0U;
  if (!SpiTransferFrame(MakeReadCmd(addr), rx_discard)) {
    return false;
  }
  if (!SpiTransferFrame(kNopCmd, rx_resp)) {
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

bool As5048aAngleSensor::ConfigureAs5048a(uint16_t& initial_angle_14bit) {
  // Clear any startup/previous error flags via Clear Error Flag (0x0001).
  uint16_t errfl = 0U;
  (void)ReadRegister(kRegClearErrorFlag, errfl);
  last_error_ = Error::kNone;

  // Note: AS5048A has no runtime filter/hysteresis/rate configuration registers
  // (14-bit resolution and 11.25 kHz internal sampling rate are fixed). Maximum
  // responsiveness is achieved via high-speed SPI (9 MHz) with minimum valid
  // CSn timing delays.

  // Poll Diagnostics + AGC (0x3FFD) until Offset Compensation Finished (OCF=1)
  // and verify the magnet position is within the acceptable range.
  bool diag_ok = false;
  for (uint32_t attempt = 0U; attempt < kDiagPollAttempts; ++attempt) {
    uint16_t diag = 0U;
    if (!ReadRegister(kRegDiagAgc, diag)) {
      return false;
    }
    last_diagnostics_ = diag;

    const bool ocf = (diag & kDiagOcfMask) != 0U;
    const bool cof = (diag & kDiagCofMask) != 0U;
    const bool comp_low = (diag & kDiagCompLowMask) != 0U;
    const bool comp_high = (diag & kDiagCompHighMask) != 0U;

    if (cof) {
      last_error_ = Error::kCordicOverflow;
      return false;
    }
    if (ocf) {
      if (comp_low) {
        last_error_ = Error::kMagnetTooStrong;
        return false;
      }
      if (comp_high) {
        last_error_ = Error::kMagnetTooWeak;
        return false;
      }
      diag_ok = true;
      break;
    }
    HAL_Delay(1);
  }
  if (!diag_ok) {
    last_error_ = Error::kDiagNotReady;
    return false;
  }

  if (!ReadRegister(kRegMagnitude, last_magnitude_)) {
    return false;
  }

  return ReadRegister(kRegAngle, initial_angle_14bit);
}

}  // namespace stfoc
