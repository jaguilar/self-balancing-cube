#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <variant>

#include "BLDCMotor.h"
#include "angle_sensor.h"
#include "as5048a_angle_sensor.h"
#include "current_sense.h"
#include "motor_driver.h"
#include "stm32_hal.h"

namespace stfoc {

struct FocLoopConfig {
  StTimerMotorConfig motor_driver_config;
  StAdcCurrentSenseConfig current_sense_config;
  std::variant<As5047pSensorConfig, As5048aSensorConfig> angle_sensor_config;

  int32_t pole_pairs = 0;
  float phase_resistance = 0.0f;
  float kv_rating = 0.0f;
  float supply_voltage = 0.0f;
  float voltage_limit = 0.0f;
  float voltage_sensor_align = 0.0f;
  float current_limit = 0.0f;
  float max_velocity_rad_s = 0.0f;
  MotionControlType controller = MotionControlType::velocity_openloop;
  TorqueControlType torque_controller = TorqueControlType::voltage;
  bool link_current_sense = false;

  hal_tim_oc_compare_unit_t adc_trigger_compare_unit{};
  uint32_t adc_trigger_delay_ns = 0U;
  uint32_t adc_irq_priority = 0U;
};

/// Owns and orchestrates the FOC motor driver, angle sensor, current sense,
/// and SimpleFOC BLDCMotor, running DoFOC() from the current-sense ADC ISR.
class FocLoop {
 public:
  /// Returns an FocLoopConfig populated from the active board_config.h
  /// defaults.
  static FocLoopConfig DefaultConfig();

  /// Constructs an FocLoop using the active board_config.h defaults.
  FocLoop();

  /// Constructs an FocLoop with an explicit configuration.
  explicit FocLoop(const FocLoopConfig& config);
  ~FocLoop();

  FocLoop(const FocLoop&) = delete;
  FocLoop& operator=(const FocLoop&) = delete;

  /// Initializes all FOC subsystems, configures the current-sense ADC trigger,
  /// and enables the ADC interrupt that drives DoFOC().
  bool Init();
  void Deinit();

  /// Executes one FOC iteration. Called from the current-sense ADC interrupt.
  void DoFOC();

  void SetTarget(float target) {
    target_.store(target, std::memory_order_relaxed);
  }
  float target() const { return target_.load(std::memory_order_relaxed); }

  bool initialized() const { return initialized_; }
  uint32_t isr_count() const {
    return isr_count_.load(std::memory_order_relaxed);
  }
  uint64_t isr_cycles_sum() const;
  uint32_t isr_cycles_max() const {
    return isr_cycles_max_.load(std::memory_order_relaxed);
  }
  void ResetIsrTiming();

  PhaseCurrent_s last_currents() const;

  StTimerMotorDriver* motor_driver() {
    return motor_driver_ ? &*motor_driver_ : nullptr;
  }
  StAdcCurrentSense* current_sense() {
    return current_sense_ ? &*current_sense_ : nullptr;
  }
  Sensor* angle_sensor() { return angle_sensor_; }
  BLDCMotor* motor() { return motor_ ? &*motor_ : nullptr; }

 private:
  static void AdcIrqTrampoline(void* ctx);

  FocLoopConfig config_;

  std::optional<StTimerMotorDriver> motor_driver_;
  std::optional<StAdcCurrentSense> current_sense_;
  std::optional<As5047pAngleSensor> as5047p_sensor_;
  std::optional<As5048aAngleSensor> as5048a_sensor_;
  Sensor* angle_sensor_{nullptr};
  std::optional<BLDCMotor> motor_;

  std::atomic<float> target_{0.0f};
  std::atomic<uint32_t> isr_count_{0U};
  uint64_t isr_cycles_sum_{0U};
  std::atomic<uint32_t> isr_cycles_max_{0U};
  float last_ia_{0.0f};
  float last_ib_{0.0f};
  float last_ic_{0.0f};
  bool initialized_{false};
};

}  // namespace stfoc
