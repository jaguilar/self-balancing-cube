#include "motor_driver.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>

#include "stm32c5xx_ll_exti.h"

namespace stfoc {
namespace {

std::array<StTimerMotorDriver*, 16> g_fault_drivers{};

uint32_t GetExtiGpioPort(hal_gpio_t port) {
  switch (port) {
    case HAL_GPIOA:
      return LL_EXTI_GPIO_PORTA;
    case HAL_GPIOB:
      return LL_EXTI_GPIO_PORTB;
    case HAL_GPIOC:
      return LL_EXTI_GPIO_PORTC;
    case HAL_GPIOD:
      return LL_EXTI_GPIO_PORTD;
    case HAL_GPIOE:
      return LL_EXTI_GPIO_PORTE;
#if defined(GPIOF)
    case HAL_GPIOF:
      return LL_EXTI_GPIO_PORTF;
#endif
#if defined(GPIOG)
    case HAL_GPIOG:
      return LL_EXTI_GPIO_PORTG;
#endif
    case HAL_GPIOH:
      return LL_EXTI_GPIO_PORTH;
    default:
      assert(false && "Unsupported GPIO port for EXTI");
      return LL_EXTI_GPIO_PORTA;
  }
}

uint32_t GetPinIndex(uint32_t pin) {
  assert(pin != 0U && std::has_single_bit(pin) && pin <= HAL_GPIO_PIN_15);
  return static_cast<uint32_t>(std::countr_zero(pin));
}

IRQn_Type GetExtiIrqn(uint32_t pin_index) {
  return static_cast<IRQn_Type>(static_cast<int>(EXTI0_IRQn) +
                                static_cast<int>(pin_index));
}

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

void EnableTimerClock(hal_tim_t instance) {
  switch (instance) {
    case HAL_TIM1:
      HAL_RCC_TIM1_EnableClock();
      break;
    case HAL_TIM8:
      HAL_RCC_TIM8_EnableClock();
      break;
    case HAL_TIM15:
      HAL_RCC_TIM15_EnableClock();
      break;
    case HAL_TIM16:
      HAL_RCC_TIM16_EnableClock();
      break;
    case HAL_TIM17:
      HAL_RCC_TIM17_EnableClock();
      break;
    default:
      assert(false && "Unsupported timer instance");
      break;
  }
}

uint32_t GetTimerClockFreq(hal_tim_t instance) {
  switch (instance) {
    case HAL_TIM1:
    case HAL_TIM8:
    case HAL_TIM15:
    case HAL_TIM16:
    case HAL_TIM17:
      return HAL_RCC_GetPCLK2Freq();
    default:
      assert(false && "Unsupported timer instance");
      return 0U;
  }
}

}  // namespace

StTimerMotorDriver::StTimerMotorDriver(const StTimerMotorConfig& config)
    : config_(config),
      phases_{config.phase_a, config.phase_b, config.phase_c} {
  voltage_power_supply = NOT_SET;
  voltage_limit = NOT_SET;
  pwm_frequency = static_cast<long>(config_.pwm_freq);
}

StTimerMotorDriver::~StTimerMotorDriver() { deinit(); }

int StTimerMotorDriver::init() {
  if (voltage_power_supply <= 0.0f || voltage_limit <= 0.0f ||
      config_.pwm_freq == 0U) {
    return 0;
  }

  inv_voltage_power_supply_ = 1.0f / voltage_power_supply;

  EnableTimerClock(config_.timer_instance);
  EnableGpioClock(config_.drv_en.port);
  if (config_.drv_coast.pin != 0U) {
    EnableGpioClock(config_.drv_coast.port);
  }
  if (config_.drv_nfault.pin != 0U) {
    EnableGpioClock(config_.drv_nfault.port);
  }
  for (const auto& phase : phases_) {
    EnableGpioClock(phase.port);
  }

  hal_gpio_config_t drv_en_cfg{};
  drv_en_cfg.mode = HAL_GPIO_MODE_OUTPUT;
  drv_en_cfg.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
  drv_en_cfg.pull = HAL_GPIO_PULL_NO;
  drv_en_cfg.speed = HAL_GPIO_SPEED_FREQ_LOW;
  drv_en_cfg.init_state = HAL_GPIO_PIN_RESET;
  if (HAL_GPIO_Init(config_.drv_en.port, config_.drv_en.pin, &drv_en_cfg) !=
      HAL_OK) {
    return 0;
  }

  if (config_.drv_coast.pin != 0U) {
    hal_gpio_config_t drv_coast_cfg{};
    drv_coast_cfg.mode = HAL_GPIO_MODE_OUTPUT;
    drv_coast_cfg.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
    drv_coast_cfg.pull = HAL_GPIO_PULL_NO;
    drv_coast_cfg.speed = HAL_GPIO_SPEED_FREQ_LOW;
    drv_coast_cfg.init_state = HAL_GPIO_PIN_RESET;
    if (HAL_GPIO_Init(config_.drv_coast.port, config_.drv_coast.pin,
                      &drv_coast_cfg) != HAL_OK) {
      return 0;
    }
  }

  if (config_.drv_nfault.pin != 0U) {
    hal_gpio_config_t drv_nfault_cfg{};
    drv_nfault_cfg.mode = HAL_GPIO_MODE_INPUT;
    drv_nfault_cfg.pull = HAL_GPIO_PULL_UP;
    if (HAL_GPIO_Init(config_.drv_nfault.port, config_.drv_nfault.pin,
                      &drv_nfault_cfg) != HAL_OK) {
      return 0;
    }
  }

  for (const auto& phase : phases_) {
    hal_gpio_config_t pwm_cfg{};
    pwm_cfg.mode = HAL_GPIO_MODE_ALTERNATE;
    pwm_cfg.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
    pwm_cfg.pull = HAL_GPIO_PULL_NO;
    pwm_cfg.speed = HAL_GPIO_SPEED_FREQ_HIGH;
    pwm_cfg.alternate = phase.alternate;
    if (HAL_GPIO_Init(phase.port, phase.pin, &pwm_cfg) != HAL_OK) {
      return 0;
    }
  }

  if (HAL_TIM_Init(&htim_, config_.timer_instance) != HAL_OK) {
    return 0;
  }

  const uint32_t tim_clk_hz = GetTimerClockFreq(config_.timer_instance);
  arr_value_ = tim_clk_hz / (2U * config_.pwm_freq);
  if (arr_value_ == 0U) {
    return 0;
  }

  hal_tim_config_t tim_cfg{};
  tim_cfg.prescaler = 0U;
  tim_cfg.counter_mode = HAL_TIM_COUNTER_CENTER_UP_DOWN;
  tim_cfg.period = arr_value_;
  tim_cfg.repetition_counter = 0U;
  tim_cfg.clock_sel.clock_source = HAL_TIM_CLK_INTERNAL;
  if (HAL_TIM_SetConfig(&htim_, &tim_cfg) != HAL_OK) {
    return 0;
  }

  if (HAL_TIM_SetDTSPrescaler(&htim_, HAL_TIM_DTS_DIV1) != HAL_OK ||
      HAL_TIM_SetDTS2Prescaler(&htim_, HAL_TIM_DTS2_DIV1) != HAL_OK ||
      HAL_TIM_EnableAutoReloadPreload(&htim_) != HAL_OK ||
      HAL_TIM_SetUpdateSource(&htim_, HAL_TIM_UPDATE_REGULAR) != HAL_OK ||
      HAL_TIM_EnableUpdateGeneration(&htim_) != HAL_OK ||
      HAL_TIM_SetTriggerOutput(&htim_, HAL_TIM_TRGO_UPDATE) != HAL_OK) {
    return 0;
  }

  hal_tim_oc_channel_config_t oc_channel_cfg{};
  oc_channel_cfg.polarity = HAL_TIM_OC_HIGH;
  oc_channel_cfg.idle_state = HAL_TIM_OC_IDLE_STATE_RESET;
  oc_channel_cfg.override_state = HAL_TIM_OC_OVERRIDE_RESET;
  oc_channel_cfg.break_mode = HAL_TIM_OC_BREAKMODE_IMMEDIATE;

  hal_tim_oc_compare_unit_config_t oc_unit_cfg{};
  oc_unit_cfg.mode = HAL_TIM_OC_PWM2;
  oc_unit_cfg.pulse = arr_value_ / 2U;

  for (const auto& phase : phases_) {
    if (HAL_TIM_OC_SetConfigChannel(&htim_, phase.channel, &oc_channel_cfg) !=
            HAL_OK ||
        HAL_TIM_OC_SetConfigCompareUnit(&htim_, phase.compare_unit,
                                        &oc_unit_cfg) != HAL_OK ||
        HAL_TIM_OC_EnableComparePreload(&htim_, phase.compare_unit) !=
            HAL_OK) {
      return 0;
    }
  }

  setPwm(voltage_power_supply * 0.5f, voltage_power_supply * 0.5f,
         voltage_power_supply * 0.5f);

  if (HAL_TIM_Start(&htim_) != HAL_OK) {
    return 0;
  }

  channel_active_ = {false, false, false};
  enabled_ = false;
  coast_mode_ = false;
  fault_latched_.store(false, std::memory_order_release);
  initialized = true;

  if (config_.drv_nfault.pin != 0U) {
    const uint32_t pin_idx = GetPinIndex(config_.drv_nfault.pin);
    const uint32_t exti_line = config_.drv_nfault.pin;
    const IRQn_Type irqn = GetExtiIrqn(pin_idx);

    g_fault_drivers[pin_idx] = this;
    LL_EXTI_SetEXTISource(GetExtiGpioPort(config_.drv_nfault.port), pin_idx);
    LL_EXTI_DisableRisingTrig_0_31(exti_line);
    LL_EXTI_EnableFallingTrig_0_31(exti_line);
    LL_EXTI_ClearFallingFlag_0_31(exti_line);
    LL_EXTI_EnableIT_0_31(exti_line);

    NVIC_SetPriority(irqn, 0U);
    NVIC_ClearPendingIRQ(irqn);
    NVIC_EnableIRQ(irqn);

    if (isFaultPinActive()) {
      fault_latched_.store(true, std::memory_order_release);
    }
  }

  return 1;
}

void StTimerMotorDriver::deinit() {
  if (!initialized) {
    return;
  }
  if (config_.drv_nfault.pin != 0U) {
    const uint32_t pin_idx = GetPinIndex(config_.drv_nfault.pin);
    const uint32_t exti_line = config_.drv_nfault.pin;
    const IRQn_Type irqn = GetExtiIrqn(pin_idx);

    NVIC_DisableIRQ(irqn);
    LL_EXTI_DisableIT_0_31(exti_line);
    LL_EXTI_DisableFallingTrig_0_31(exti_line);
    LL_EXTI_ClearFallingFlag_0_31(exti_line);
    NVIC_ClearPendingIRQ(irqn);
    if (g_fault_drivers[pin_idx] == this) {
      g_fault_drivers[pin_idx] = nullptr;
    }
    HAL_GPIO_DeInit(config_.drv_nfault.port, config_.drv_nfault.pin);
  }
  disable();
  (void)HAL_TIM_Stop(&htim_);
  (void)HAL_TIM_DeInit(&htim_);
  for (const auto& phase : phases_) {
    HAL_GPIO_DeInit(phase.port, phase.pin);
  }
  if (config_.drv_coast.pin != 0U) {
    HAL_GPIO_DeInit(config_.drv_coast.port, config_.drv_coast.pin);
  }
  HAL_GPIO_DeInit(config_.drv_en.port, config_.drv_en.pin);
  initialized = false;
}

void StTimerMotorDriver::enable() {
  if (!initialized) {
    return;
  }
  if (isFaultPinActive()) {
    fault_latched_.store(true, std::memory_order_release);
  }
  if (hasFault()) {
    return;
  }
  HAL_GPIO_WritePin(config_.drv_en.port, config_.drv_en.pin, HAL_GPIO_PIN_SET);
  HAL_Delay(1);
  if (hasFault() || isFaultPinActive()) {
    fault_latched_.store(true, std::memory_order_release);
    disable();
    return;
  }
  enabled_ = true;
  updateCoastPin();

  for (std::size_t i = 0; i < phases_.size(); ++i) {
    if (phase_state_[i] == PhaseState::PHASE_ON && !channel_active_[i]) {
      (void)HAL_TIM_OC_StartChannel(&htim_, phases_[i].channel);
      channel_active_[i] = true;
    }
  }
}

void StTimerMotorDriver::disable() {
  if (!initialized) {
    return;
  }
  for (std::size_t i = 0; i < phases_.size(); ++i) {
    if (channel_active_[i]) {
      (void)HAL_TIM_OC_StopChannel(&htim_, phases_[i].channel);
      channel_active_[i] = false;
    }
  }
  enabled_ = false;
  updateCoastPin();
  HAL_GPIO_WritePin(config_.drv_en.port, config_.drv_en.pin,
                    HAL_GPIO_PIN_RESET);
}

void StTimerMotorDriver::setCoast(bool coast) {
  coast_mode_ = coast;
  if (initialized) {
    updateCoastPin();
  }
}

bool StTimerMotorDriver::isFaultPinActive() const {
  if (config_.drv_nfault.pin == 0U) {
    return false;
  }
  return HAL_GPIO_ReadPin(config_.drv_nfault.port, config_.drv_nfault.pin) ==
         HAL_GPIO_PIN_RESET;
}

void StTimerMotorDriver::handleFaultIrq() {
  if (config_.drv_nfault.pin != 0U) {
    LL_EXTI_ClearFallingFlag_0_31(config_.drv_nfault.pin);
  }
  fault_latched_.store(true, std::memory_order_release);
  disable();
}

void StTimerMotorDriver::updateCoastPin() {
  if (config_.drv_coast.pin == 0U) {
    return;
  }
  const hal_gpio_pin_state_t state =
      (enabled_ && !coast_mode_) ? HAL_GPIO_PIN_SET : HAL_GPIO_PIN_RESET;
  HAL_GPIO_WritePin(config_.drv_coast.port, config_.drv_coast.pin, state);
}

void StTimerMotorDriver::setPwm(float ua, float ub, float uc) {
  ua = std::clamp(ua, 0.0f, voltage_limit);
  ub = std::clamp(ub, 0.0f, voltage_limit);
  uc = std::clamp(uc, 0.0f, voltage_limit);

  dc_a = std::clamp(ua * inv_voltage_power_supply_, 0.0f, 1.0f);
  dc_b = std::clamp(ub * inv_voltage_power_supply_, 0.0f, 1.0f);
  dc_c = std::clamp(uc * inv_voltage_power_supply_, 0.0f, 1.0f);

  const auto to_pulse = [this](float duty) -> uint32_t {
    const float scaled = (1.0f - duty) * static_cast<float>(arr_value_);
    const long rounded = std::lround(scaled);
    return static_cast<uint32_t>(
        std::clamp<long>(rounded, 0L, static_cast<long>(arr_value_)));
  };

  (void)HAL_TIM_OC_SetCompareUnitPulse(&htim_, phases_[0].compare_unit,
                                       to_pulse(dc_a));
  (void)HAL_TIM_OC_SetCompareUnitPulse(&htim_, phases_[1].compare_unit,
                                       to_pulse(dc_b));
  (void)HAL_TIM_OC_SetCompareUnitPulse(&htim_, phases_[2].compare_unit,
                                       to_pulse(dc_c));
}

void StTimerMotorDriver::setPhaseState(PhaseState sa, PhaseState sb,
                                       PhaseState sc) {
  assert(sa != PhaseState::PHASE_LO && sb != PhaseState::PHASE_LO &&
         sc != PhaseState::PHASE_LO && sa != PhaseState::PHASE_HI &&
         sb != PhaseState::PHASE_HI && sc != PhaseState::PHASE_HI &&
         "PHASE_LO and PHASE_HI states are not supported in "
         "StTimerMotorDriver");

  const std::array<PhaseState, 3> next_states{sa, sb, sc};
  for (std::size_t i = 0; i < phases_.size(); ++i) {
    phase_state_[i] = next_states[i];
    if (!enabled_) {
      continue;
    }
    const bool want_active = (phase_state_[i] == PhaseState::PHASE_ON);
    if (want_active && !channel_active_[i]) {
      (void)HAL_TIM_OC_StartChannel(&htim_, phases_[i].channel);
      channel_active_[i] = true;
    } else if (!want_active && channel_active_[i]) {
      (void)HAL_TIM_OC_StopChannel(&htim_, phases_[i].channel);
      channel_active_[i] = false;
    }
  }
}

}  // namespace stfoc

extern "C" void EXTI7_IRQHandler(void) {
  if (auto* driver = stfoc::g_fault_drivers[7]; driver != nullptr) {
    driver->handleFaultIrq();
  } else {
    LL_EXTI_ClearFallingFlag_0_31(LL_EXTI_LINE_7);
  }
}
