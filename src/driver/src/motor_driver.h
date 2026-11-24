#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "stm32_hal.h"
#include "common/base_classes/BLDCDriver.h"

namespace stfoc {

struct PwmChannelConfig {
  hal_gpio_t port;
  uint32_t pin;
  hal_gpio_af_t alternate;
  hal_tim_channel_t channel;
  hal_tim_oc_compare_unit_t compare_unit;
};

struct GpioPinConfig {
  hal_gpio_t port;
  uint32_t pin;
};

struct StTimerMotorConfig {
  hal_tim_t timer_instance;
  PwmChannelConfig phase_a;
  PwmChannelConfig phase_b;
  PwmChannelConfig phase_c;
  GpioPinConfig drv_en;
  GpioPinConfig drv_coast;
  GpioPinConfig drv_nfault;
  uint32_t pwm_freq;
};

class StTimerMotorDriver : public BLDCDriver {
 public:
  explicit StTimerMotorDriver(const StTimerMotorConfig& config);
  ~StTimerMotorDriver();

  StTimerMotorDriver(const StTimerMotorDriver&) = delete;
  StTimerMotorDriver& operator=(const StTimerMotorDriver&) = delete;

  int init() override;
  void Deinit();
  void enable() override;
  void disable() override;
  void setPwm(float ua, float ub, float uc) override;
  void setPhaseState(PhaseState sa, PhaseState sb, PhaseState sc) override;

  void SetCoast(bool coast);
  bool IsCoasting() const { return coast_mode_; }

  bool HasFault() const {
    return fault_latched_.load(std::memory_order_acquire);
  }
  bool IsFaultPinActive() const;
  void ClearFault() { fault_latched_.store(false, std::memory_order_release); }

  void HandleFaultIrq();

  hal_tim_handle_t* timer_handle() { return &htim_; }
  const hal_tim_handle_t* timer_handle() const { return &htim_; }

 private:
  void UpdateCoastPin();

  StTimerMotorConfig config_;
  std::array<PwmChannelConfig, 3> phases_;
  std::array<PhaseState, 3> phase_state_{
      PhaseState::PHASE_ON,
      PhaseState::PHASE_ON,
      PhaseState::PHASE_ON,
  };
  std::array<bool, 3> channel_active_{false, false, false};

  hal_tim_handle_t htim_{};
  uint32_t arr_value_{0};
  float inv_voltage_power_supply_{0.0f};
  bool enabled_{false};
  bool coast_mode_{false};
  std::atomic<bool> fault_latched_{false};
};

}  // namespace stfoc
