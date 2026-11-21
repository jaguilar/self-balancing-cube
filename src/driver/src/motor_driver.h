#pragma once

#include <array>
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
  uint32_t pwm_freq;
};

constexpr StTimerMotorConfig DefaultMotorDriverConfig(uint32_t pwm_freq = 25000U) {
  return StTimerMotorConfig{
      .timer_instance = HAL_TIM1,
      .phase_a =
          {
              .port = HAL_GPIOA,
              .pin = HAL_GPIO_PIN_8,
              .alternate = HAL_GPIO_AF_1,
              .channel = HAL_TIM_CHANNEL_1,
              .compare_unit = HAL_TIM_OC_COMPARE_UNIT_1,
          },
      .phase_b =
          {
              .port = HAL_GPIOA,
              .pin = HAL_GPIO_PIN_9,
              .alternate = HAL_GPIO_AF_1,
              .channel = HAL_TIM_CHANNEL_2,
              .compare_unit = HAL_TIM_OC_COMPARE_UNIT_2,
          },
      .phase_c =
          {
              .port = HAL_GPIOA,
              .pin = HAL_GPIO_PIN_11,
              .alternate = HAL_GPIO_AF_1,
              .channel = HAL_TIM_CHANNEL_4,
              .compare_unit = HAL_TIM_OC_COMPARE_UNIT_4,
          },
      .drv_en =
          {
              .port = HAL_GPIOB,
              .pin = HAL_GPIO_PIN_6,
          },
      .pwm_freq = pwm_freq,
  };
}

class StTimerMotorDriver : public BLDCDriver {
 public:
  explicit StTimerMotorDriver(const StTimerMotorConfig& config);
  ~StTimerMotorDriver();

  StTimerMotorDriver(const StTimerMotorDriver&) = delete;
  StTimerMotorDriver& operator=(const StTimerMotorDriver&) = delete;

  int init() override;
  void deinit();
  void enable() override;
  void disable() override;
  void setPwm(float ua, float ub, float uc) override;
  void setPhaseState(PhaseState sa, PhaseState sb, PhaseState sc) override;

 private:
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
};

}  // namespace stfoc
