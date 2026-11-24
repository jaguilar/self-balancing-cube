#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "src/common/motor_protocol.h"
#include "stm32_hal.h"

extern "C" void FDCAN1_IT0_IRQHandler(void);
extern "C" void HAL_FDCAN_RxFifo0Callback(hal_fdcan_handle_t* hfdcan,
                                          uint32_t rx_fifo0_interrupts);

namespace stfoc {

class CanDriverTestPeer;

struct CanPinConfig {
  hal_gpio_t port;
  uint32_t pin;
  hal_gpio_af_t alternate;
};

struct StCanDriverConfig {
  hal_fdcan_t fdcan_instance;
  CanPinConfig tx;
  CanPinConfig rx;
  // Default: 500 kbps nominal at 144 MHz PCLK1 (87.5% sample point).
  hal_fdcan_nominal_bit_timing_t nominal_bit_timing = {
      .nominal_prescaler = 1U,
      .nominal_jump_width = 1U,
      .nominal_time_seg1 = 251U,
      .nominal_time_seg2 = 36U,
  };
  hal_fdcan_frame_format_t frame_format = HAL_FDCAN_FRAME_FORMAT_FD_NO_BRS;
  hal_fdcan_mode_t mode = HAL_FDCAN_MODE_NORMAL;
  bool enable_gpio = true;
  // Total number of motor drivers on the bus (1..kNumMotors).
  // Setting this to 1 enables single-controller, single-motor testing.
  size_t num_motors = kNumMotors;
  // NVIC preemption priority for FDCAN1_IT0_IRQn. Must be numerically greater
  // (lower priority) than the motor control ISR so double-buffered reads from
  // the motor control ISR are never preempted by CAN RX updates.
  hal_cortex_nvic_preemp_priority_t rx_irq_priority =
      HAL_CORTEX_NVIC_PREEMP_PRIORITY_5;
  // If non-zero, overrides the hardware STM32C5 unique ID (useful for tests).
  uint64_t device_id_override = 0U;
};

/// Interrupt-driven FDCAN communication helper for a motor driver.
///
/// Incoming frames are processed automatically in the FDCAN RX FIFO 0
/// interrupt. The latest MotorCommand is double-buffered with release/acquire
/// atomics so a higher-priority motor control ISR can read last_command()
/// without locking and without ever observing a partially written command.
class StCanDriver {
 public:
  enum class State : uint8_t {
    kInitializing = 0,
    kReady,
    kError,
  };

  enum class Error : uint8_t {
    kOk = 0,
    kNotInitialized,
    kInvalidConfig,
    kHalError,
    kTxFull,
    kProtocolError,
  };

  explicit StCanDriver(const StCanDriverConfig& config);
  ~StCanDriver();

  StCanDriver(const StCanDriver&) = delete;
  StCanDriver& operator=(const StCanDriver&) = delete;

  /// Initializes the FDCAN peripheral, enables RX FIFO 0 interrupts, starts
  /// the bus, and broadcasts this driver's unique ID.
  Error Init();

  /// Disables FDCAN interrupts, stops and deinitializes the peripheral and
  /// (if enabled) its GPIOs.
  void Deinit();

  /// Current state machine state (initializing, ready, or error).
  State state() const { return state_.load(std::memory_order_acquire); }

  /// Most recent error code (Error::kOk when healthy).
  Error error() const { return error_.load(std::memory_order_acquire); }

  /// This motor driver's assigned index (0..num_motors-1) once discovery
  /// completes, or std::nullopt while initializing.
  std::optional<uint8_t> motor_index() const {
    const uint8_t idx = motor_index_.load(std::memory_order_acquire);
    if (idx == kUnassignedMotorIndex) {
      return std::nullopt;
    }
    return idx;
  }

  /// Latest decoded MotorCommand from the active double-buffer slot.
  /// Wait-free and tear-free when called from a higher-priority ISR
  /// (such as the motor control interrupt).
  std::optional<MotorCommand> last_command() const {
    if (command_count_.load(std::memory_order_acquire) == 0U) {
      return std::nullopt;
    }
    const uint8_t idx = command_active_idx_.load(std::memory_order_acquire);
    return command_buf_[idx];
  }

  /// Updates the current motor speed (rad/s) sent in response to incoming
  /// controller commands. Safe to call from the higher-priority motor control
  /// ISR.
  void SetCurrentSpeed(float speed_rad_s) {
    current_speed_rad_s_.store(speed_rad_s, std::memory_order_relaxed);
  }

 private:
  friend class CanDriverTestPeer;
  friend void ::FDCAN1_IT0_IRQHandler(void);
  friend void ::HAL_FDCAN_RxFifo0Callback(hal_fdcan_handle_t* hfdcan,
                                          uint32_t rx_fifo0_interrupts);

  static constexpr uint8_t kUnassignedMotorIndex = 0xFFU;

  static uint64_t ReadDeviceUniqueId64();

  Error Fail(Error err);
  Error BroadcastId();
  bool RegisterDiscoveredId(uint64_t id);
  Error SendSpeed(float speed_rad_s);
  Error TransmitFrame(uint32_t std_id, std::span<const uint8_t> data);
  void HandleRxFifo0Interrupt();
  void ProcessRxFrame(uint32_t std_id, std::span<const uint8_t> payload);

  StCanDriverConfig config_;
  hal_fdcan_handle_t hfdcan_{};
  bool initialized_{false};
  uint64_t self_id_{0U};

  std::atomic<State> state_{State::kInitializing};
  std::atomic<Error> error_{Error::kNotInitialized};

  std::array<uint64_t, kNumMotors> discovered_ids_{};
  std::atomic<size_t> discovered_count_{0U};
  std::atomic<uint8_t> motor_index_{kUnassignedMotorIndex};

  // Double-buffered MotorCommand written by CAN RX ISR, read by motor control ISR.
  std::array<MotorCommand, 2> command_buf_{};
  std::atomic<uint8_t> command_active_idx_{0U};
  std::atomic<uint32_t> command_count_{0U};

  // Double-buffered received speed telemetry (used by loopback tests).
  std::array<DriverSpeedMessage, 2> received_speed_buf_{};
  std::array<uint32_t, 2> received_speed_can_id_buf_{};
  std::atomic<uint8_t> speed_active_idx_{0U};
  std::atomic<uint32_t> received_speed_count_{0U};

  std::atomic<float> current_speed_rad_s_{0.0f};
};

}  // namespace stfoc
