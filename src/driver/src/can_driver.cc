#include "can_driver.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

namespace stfoc {

namespace {

std::atomic<StCanDriver*> g_active_can_driver{nullptr};

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
    case HAL_GPIOH:
      HAL_RCC_GPIOH_EnableClock();
      break;
    default:
      break;
  }
}

bool LengthToDlc(size_t len, hal_fdcan_data_length_code_t& dlc_out) {
  if (len <= 8U) {
    dlc_out = static_cast<hal_fdcan_data_length_code_t>(len);
    return true;
  }
  if (len <= 12U) {
    dlc_out = HAL_FDCAN_DATA_LEN_FDCAN_12_BYTE;
    return true;
  }
  if (len <= 16U) {
    dlc_out = HAL_FDCAN_DATA_LEN_FDCAN_16_BYTE;
    return true;
  }
  if (len <= 20U) {
    dlc_out = HAL_FDCAN_DATA_LEN_FDCAN_20_BYTE;
    return true;
  }
  if (len <= 24U) {
    dlc_out = HAL_FDCAN_DATA_LEN_FDCAN_24_BYTE;
    return true;
  }
  if (len <= 32U) {
    dlc_out = HAL_FDCAN_DATA_LEN_FDCAN_32_BYTE;
    return true;
  }
  if (len <= 48U) {
    dlc_out = HAL_FDCAN_DATA_LEN_FDCAN_48_BYTE;
    return true;
  }
  if (len <= 64U) {
    dlc_out = HAL_FDCAN_DATA_LEN_FDCAN_64_BYTE;
    return true;
  }
  return false;
}

size_t DlcToLength(hal_fdcan_data_length_code_t dlc) {
  static constexpr std::array<uint8_t, 16> kDlcBytes = {
      0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U,
  };
  const auto idx = static_cast<size_t>(dlc);
  return idx < kDlcBytes.size() ? static_cast<size_t>(kDlcBytes[idx]) : 0U;
}

}  // namespace

StCanDriver::StCanDriver(const StCanDriverConfig& config) : config_(config) {}

StCanDriver::~StCanDriver() { Deinit(); }

StCanDriver::Error StCanDriver::Fail(Error err) {
  error_.store(err, std::memory_order_release);
  state_.store(State::kError, std::memory_order_release);
  return err;
}

uint64_t StCanDriver::ReadDeviceUniqueId64() {
  hal_device_uid_t uid{};
  HAL_GetDeviceUniqueID(&uid);
  // Fold all 96 bits of the STM32C5 manufacturing UID deterministically into
  // 64 bits so wafer X/Y, wafer number, and lot ASCII all contribute.
  const uint64_t low_high =
      (static_cast<uint64_t>(uid.uid_1) << 32U) | static_cast<uint64_t>(uid.uid_0);
  const uint64_t mixed_uid2 =
      static_cast<uint64_t>(uid.uid_2) * 0x9E3779B97F4A7C15ULL;
  return low_high ^ mixed_uid2;
}

StCanDriver::Error StCanDriver::Init() {
  if (initialized_) {
    Deinit();
  }

  if (config_.num_motors == 0U || config_.num_motors > kNumMotors) {
    return Fail(Error::kInvalidConfig);
  }

  HAL_RCC_FDCAN_EnableClock();

  if (HAL_FDCAN_Init(&hfdcan_, config_.fdcan_instance) != HAL_OK) {
    return Fail(Error::kHalError);
  }

  if (HAL_RCC_FDCAN_SetKernelClkSource(HAL_RCC_FDCAN_CLK_SRC_PCLK1) != HAL_OK) {
    (void)HAL_FDCAN_DeInit(&hfdcan_);
    return Fail(Error::kHalError);
  }

  hal_fdcan_config_t fdcan_cfg{};
  fdcan_cfg.nominal_bit_timing = config_.nominal_bit_timing;
  fdcan_cfg.frame_format = config_.frame_format;
  fdcan_cfg.mode = config_.mode;
  fdcan_cfg.auto_retransmission = HAL_FDCAN_AUTO_RETRANSMISSION_ENABLE;
  fdcan_cfg.transmit_pause = HAL_FDCAN_TRANSMIT_PAUSE_DISABLE;
  fdcan_cfg.protocol_exception = HAL_FDCAN_PROTOCOL_EXCEPTION_ENABLE;
  fdcan_cfg.std_filters_nbr = 0U;
  fdcan_cfg.ext_filters_nbr = 0U;
  fdcan_cfg.tx_fifo_queue_mode = HAL_FDCAN_TX_MODE_FIFO;

  if (HAL_FDCAN_SetConfig(&hfdcan_, &fdcan_cfg) != HAL_OK) {
    (void)HAL_FDCAN_DeInit(&hfdcan_);
    return Fail(Error::kHalError);
  }

  if (HAL_FDCAN_SetClockDivider(&hfdcan_, HAL_FDCAN_CLOCK_DIV_1) != HAL_OK) {
    (void)HAL_FDCAN_DeInit(&hfdcan_);
    return Fail(Error::kHalError);
  }

  hal_fdcan_global_filter_config_t global_filter_cfg{};
  global_filter_cfg.acceptance_non_matching_std =
      HAL_FDCAN_NO_MATCH_TO_RX_FIFO_0;
  global_filter_cfg.acceptance_non_matching_ext = HAL_FDCAN_NO_MATCH_REJECT;
  global_filter_cfg.acceptance_remote_std = HAL_FDCAN_REMOTE_REJECT;
  global_filter_cfg.acceptance_remote_ext = HAL_FDCAN_REMOTE_REJECT;
  if (HAL_FDCAN_SetGlobalFilter(&hfdcan_, &global_filter_cfg) != HAL_OK) {
    (void)HAL_FDCAN_DeInit(&hfdcan_);
    return Fail(Error::kHalError);
  }

  if (HAL_FDCAN_SetInterruptGroupsToLine(&hfdcan_, HAL_FDCAN_IT_GROUP_RX_FIFO_0,
                                         HAL_FDCAN_IT_LINE_0) != HAL_OK) {
    (void)HAL_FDCAN_DeInit(&hfdcan_);
    return Fail(Error::kHalError);
  }
  if (HAL_FDCAN_EnableInterrupts(&hfdcan_, HAL_FDCAN_IT_RX_FIFO_0_NEW_MSG) !=
      HAL_OK) {
    (void)HAL_FDCAN_DeInit(&hfdcan_);
    return Fail(Error::kHalError);
  }
  if (HAL_FDCAN_EnableInterruptLines(&hfdcan_, HAL_FDCAN_IT_LINE_0) != HAL_OK) {
    (void)HAL_FDCAN_DeInit(&hfdcan_);
    return Fail(Error::kHalError);
  }

  if (config_.enable_gpio) {
    EnableGpioClock(config_.tx.port);
    EnableGpioClock(config_.rx.port);

    hal_gpio_config_t gpio_cfg{};
    gpio_cfg.mode = HAL_GPIO_MODE_ALTERNATE;
    gpio_cfg.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
    gpio_cfg.pull = HAL_GPIO_PULL_NO;
    gpio_cfg.speed = HAL_GPIO_SPEED_FREQ_HIGH;

    gpio_cfg.alternate = config_.tx.alternate;
    HAL_GPIO_Init(config_.tx.port, config_.tx.pin, &gpio_cfg);

    gpio_cfg.alternate = config_.rx.alternate;
    HAL_GPIO_Init(config_.rx.port, config_.rx.pin, &gpio_cfg);
  }

  self_id_ = (config_.device_id_override != 0U) ? config_.device_id_override
                                                : ReadDeviceUniqueId64();
  discovered_ids_.fill(0U);
  discovered_count_.store(0U, std::memory_order_relaxed);
  motor_index_.store(kUnassignedMotorIndex, std::memory_order_relaxed);
  command_buf_.fill(MotorCommand{});
  command_active_idx_.store(0U, std::memory_order_relaxed);
  command_count_.store(0U, std::memory_order_relaxed);
  received_speed_buf_.fill(DriverSpeedMessage{});
  received_speed_can_id_buf_.fill(0U);
  speed_active_idx_.store(0U, std::memory_order_relaxed);
  received_speed_count_.store(0U, std::memory_order_relaxed);
  current_speed_rad_s_.store(0.0f, std::memory_order_relaxed);
  error_.store(Error::kOk, std::memory_order_relaxed);
  state_.store(State::kInitializing, std::memory_order_relaxed);

  g_active_can_driver.store(this, std::memory_order_release);
  initialized_ = true;

  if (HAL_FDCAN_Start(&hfdcan_) != HAL_OK) {
    g_active_can_driver.store(nullptr, std::memory_order_release);
    initialized_ = false;
    (void)HAL_FDCAN_DeInit(&hfdcan_);
    return Fail(Error::kHalError);
  }

  // Register our own ID (which immediately transitions to kReady when
  // num_motors == 1) and broadcast it to peers on the bus.
  RegisterDiscoveredId(self_id_);

  HAL_CORTEX_NVIC_SetPriority(FDCAN1_IT0_IRQn, config_.rx_irq_priority,
                              HAL_CORTEX_NVIC_SUB_PRIORITY_0);
  NVIC_ClearPendingIRQ(FDCAN1_IT0_IRQn);
  HAL_CORTEX_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);

  const Error tx_err = BroadcastId();
  if (tx_err != Error::kOk) {
    return Fail(tx_err);
  }
  return Error::kOk;
}

void StCanDriver::Deinit() {
  if (!initialized_) {
    return;
  }
  HAL_CORTEX_NVIC_DisableIRQ(FDCAN1_IT0_IRQn);
  NVIC_ClearPendingIRQ(FDCAN1_IT0_IRQn);
  g_active_can_driver.store(nullptr, std::memory_order_release);

  (void)HAL_FDCAN_Stop(&hfdcan_);
  (void)HAL_FDCAN_DeInit(&hfdcan_);
  HAL_RCC_FDCAN_Reset();
  HAL_RCC_FDCAN_DisableClock();

  if (config_.enable_gpio) {
    HAL_GPIO_DeInit(config_.tx.port, config_.tx.pin);
    HAL_GPIO_DeInit(config_.rx.port, config_.rx.pin);
  }
  initialized_ = false;
  state_.store(State::kInitializing, std::memory_order_release);
  error_.store(Error::kNotInitialized, std::memory_order_release);
}

StCanDriver::Error StCanDriver::TransmitFrame(uint32_t std_id,
                                              std::span<const uint8_t> data) {
  if (!initialized_) {
    return Error::kNotInitialized;
  }
  if (std_id > 0x7FFU) {
    return Error::kProtocolError;
  }
  hal_fdcan_data_length_code_t dlc{};
  if (!LengthToDlc(data.size(), dlc)) {
    return Error::kProtocolError;
  }
  if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan_) ==
      HAL_FDCAN_TX_FIFO_FREE_LEVEL_0) {
    return Error::kTxFull;
  }

  std::array<uint8_t, 64> tx_buf{};
  std::memcpy(tx_buf.data(), data.data(), data.size());

  hal_fdcan_tx_header_t tx_header{};
  tx_header.d64 = 0U;
  tx_header.b.identifier = std_id;
  tx_header.b.frame_type = HAL_FDCAN_FRAME_DATA;
  tx_header.b.identifier_type = HAL_FDCAN_ID_STANDARD;
  tx_header.b.error_state_indicator = HAL_FDCAN_ERROR_STATE_IND_ACTIVE;
  tx_header.b.data_length = dlc;
  tx_header.b.bit_rate_switch = HAL_FDCAN_BIT_RATE_SWITCH_OFF;
  tx_header.b.frame_format =
      (config_.frame_format == HAL_FDCAN_FRAME_FORMAT_CLASSIC_CAN)
          ? HAL_FDCAN_HEADER_FRAME_FORMAT_CAN
          : HAL_FDCAN_HEADER_FRAME_FORMAT_FD_CAN;
  tx_header.b.event_fifo_control = HAL_FDCAN_TX_EVENTS_FIFO_DISCARD;
  tx_header.b.message_marker = 0U;

  if (HAL_FDCAN_ReqTransmitMsgFromFIFOQ(&hfdcan_, &tx_header, tx_buf.data()) !=
      HAL_OK) {
    return Error::kHalError;
  }
  return Error::kOk;
}

StCanDriver::Error StCanDriver::BroadcastId() {
  if (!initialized_) {
    return Error::kNotInitialized;
  }
  std::array<uint8_t, kDriverMessageSize> payload{};
  if (!DriverEncode(DriverIdMessage{.id = self_id_}, payload)) {
    return Error::kProtocolError;
  }
  return TransmitFrame(kCanIdDriverAnnounceId, payload);
}

bool StCanDriver::RegisterDiscoveredId(uint64_t id) {
  const size_t count = discovered_count_.load(std::memory_order_relaxed);
  for (size_t i = 0; i < count; ++i) {
    if (discovered_ids_[i] == id) {
      return motor_index_.load(std::memory_order_relaxed) !=
             kUnassignedMotorIndex;
    }
  }
  size_t new_count = count;
  if (new_count < config_.num_motors) {
    discovered_ids_[new_count++] = id;
  }
  if (new_count == config_.num_motors &&
      motor_index_.load(std::memory_order_relaxed) == kUnassignedMotorIndex) {
    std::sort(discovered_ids_.begin(),
              discovered_ids_.begin() + static_cast<ptrdiff_t>(config_.num_motors));
    for (size_t i = 0; i < config_.num_motors; ++i) {
      if (discovered_ids_[i] == self_id_) {
        motor_index_.store(static_cast<uint8_t>(i), std::memory_order_release);
        state_.store(State::kReady, std::memory_order_release);
        break;
      }
    }
  }
  discovered_count_.store(new_count, std::memory_order_release);
  return motor_index_.load(std::memory_order_relaxed) != kUnassignedMotorIndex;
}

StCanDriver::Error StCanDriver::SendSpeed(float speed_rad_s) {
  if (!initialized_) {
    return Error::kNotInitialized;
  }
  const uint8_t idx = motor_index_.load(std::memory_order_acquire);
  if (idx == kUnassignedMotorIndex) {
    return Error::kNotInitialized;
  }
  std::array<uint8_t, kDriverMessageSize> payload{};
  if (!DriverEncode(DriverSpeedMessage{.speed = speed_rad_s}, payload)) {
    return Error::kProtocolError;
  }
  return TransmitFrame(DriverSpeedCanId(idx), payload);
}

void StCanDriver::HandleRxFifo0Interrupt() {
  uint32_t fill_level = 0U;
  HAL_FDCAN_GetRxFifoFillLevel(&hfdcan_, HAL_FDCAN_RX_FIFO_0, &fill_level);
  while (fill_level > 0U) {
    hal_fdcan_rx_header_t rx_header{};
    std::array<uint8_t, 64> rx_buf{};
    if (HAL_FDCAN_GetReceivedMessage(&hfdcan_, HAL_FDCAN_RX_FIFO_0, &rx_header,
                                     rx_buf.data()) != HAL_OK) {
      break;
    }

    if (rx_header.b.identifier_type == HAL_FDCAN_ID_STANDARD &&
        rx_header.b.frame_type == HAL_FDCAN_FRAME_DATA) {
      const size_t len = DlcToLength(rx_header.b.data_length);
      ProcessRxFrame(rx_header.b.identifier,
                     std::span<const uint8_t>(rx_buf.data(), len));
    }

    HAL_FDCAN_GetRxFifoFillLevel(&hfdcan_, HAL_FDCAN_RX_FIFO_0, &fill_level);
  }
}

void StCanDriver::ProcessRxFrame(uint32_t std_id,
                                 std::span<const uint8_t> payload) {
  if (std_id == kCanIdDriverAnnounceId) {
    DriverMessage msg;
    if (ControllerDecode(payload, msg) &&
        std::holds_alternative<DriverIdMessage>(msg)) {
      RegisterDiscoveredId(std::get<DriverIdMessage>(msg).id);
    }
    return;
  }

  if (std_id == kCanIdControllerCommand) {
    const uint8_t idx = motor_index_.load(std::memory_order_relaxed);
    if (idx == kUnassignedMotorIndex) {
      return;
    }
    ControllerMessage msg;
    if (DriverDecode(payload, idx, msg) &&
        std::holds_alternative<MotorCommand>(msg)) {
      const uint8_t next_slot =
          1U - command_active_idx_.load(std::memory_order_relaxed);
      command_buf_[next_slot] = std::get<MotorCommand>(msg);
      command_active_idx_.store(next_slot, std::memory_order_release);
      command_count_.fetch_add(1U, std::memory_order_release);

      (void)SendSpeed(current_speed_rad_s_.load(std::memory_order_relaxed));
    }
    return;
  }

  if (std_id >= kCanIdDriverSpeedBase &&
      std_id < (kCanIdDriverSpeedBase + static_cast<uint32_t>(kNumMotors))) {
    DriverMessage msg;
    if (ControllerDecode(payload, msg) &&
        std::holds_alternative<DriverSpeedMessage>(msg)) {
      const uint8_t next_slot =
          1U - speed_active_idx_.load(std::memory_order_relaxed);
      received_speed_buf_[next_slot] = std::get<DriverSpeedMessage>(msg);
      received_speed_can_id_buf_[next_slot] = std_id;
      speed_active_idx_.store(next_slot, std::memory_order_release);
      received_speed_count_.fetch_add(1U, std::memory_order_release);
    }
  }
}

}  // namespace stfoc

extern "C" void FDCAN1_IT0_IRQHandler(void) {
  stfoc::StCanDriver* driver =
      stfoc::g_active_can_driver.load(std::memory_order_acquire);
  if (driver != nullptr) {
    HAL_FDCAN_Line0_IRQHandler(&driver->hfdcan_);
  }
}

extern "C" void HAL_FDCAN_RxFifo0Callback(hal_fdcan_handle_t* hfdcan,
                                          uint32_t rx_fifo0_interrupts) {
  (void)rx_fifo0_interrupts;
  stfoc::StCanDriver* driver =
      stfoc::g_active_can_driver.load(std::memory_order_acquire);
  if (driver != nullptr && &driver->hfdcan_ == hfdcan) {
    driver->HandleRxFifo0Interrupt();
  }
}
