#include "foc_loop.h"

#include <cmath>

#include "board_config.h"
#include "cordic_trig.h"

namespace stfoc {

FocLoopConfig FocLoop::DefaultConfig() {
  const board::FocDefaults defaults = board::GetFocDefaults();
  return FocLoopConfig{
      .motor_driver_config = board::GetMotorDriverConfig(),
      .current_sense_config = board::GetCurrentSenseConfig(),
      .angle_sensor_config = board::GetAngleSensorConfig(),
      .pole_pairs = defaults.pole_pairs,
      .phase_resistance = defaults.phase_resistance,
      .kv_rating = defaults.kv_rating,
      .supply_voltage = defaults.supply_voltage,
      .voltage_limit = defaults.voltage_limit,
      .voltage_sensor_align = defaults.voltage_sensor_align,
      .current_limit = defaults.current_limit,
      .max_velocity_rad_s = defaults.max_velocity_rad_s,
      .adc_trigger_compare_unit = defaults.adc_trigger_compare_unit,
      .adc_trigger_delay_ns = defaults.adc_trigger_delay_ns,
      .adc_irq_priority = defaults.adc_irq_priority,
  };
}

FocLoop::FocLoop() : FocLoop(DefaultConfig()) {}

FocLoop::FocLoop(const FocLoopConfig& config) : config_(config) {}

FocLoop::~FocLoop() { Deinit(); }

bool FocLoop::Init() {
  if (initialized_) {
    return true;
  }

  InitCordicTrig();

  // 1. Initialize motor PWM driver first so TIM1 is running for both the
  //    AS5048A DMA trigger and the current-sense ADC TRGO2 trigger.
  motor_driver_.emplace(config_.motor_driver_config);
  motor_driver_->voltage_power_supply = config_.supply_voltage;
  motor_driver_->voltage_limit = config_.supply_voltage;
  if (motor_driver_->init() != 1) {
    Deinit();
    return false;
  }

  // 2. Initialize angle sensor.
  if (std::holds_alternative<As5048aSensorConfig>(
          config_.angle_sensor_config)) {
    as5048a_sensor_.emplace(
        std::get<As5048aSensorConfig>(config_.angle_sensor_config));
    as5048a_sensor_->init();
    if (!as5048a_sensor_->initialized()) {
      Deinit();
      return false;
    }
    angle_sensor_ = &*as5048a_sensor_;
  } else {
    as5047p_sensor_.emplace(
        std::get<As5047pSensorConfig>(config_.angle_sensor_config));
    as5047p_sensor_->init();
    if (!as5047p_sensor_->initialized()) {
      Deinit();
      return false;
    }
    angle_sensor_ = &*as5047p_sensor_;
  }

  // 3. Enable the gate driver (at 50% duty / 0 V differential) so the DRV8304
  //    current-sense amplifiers are powered on during CAL zero-offset
  //    calibration, then initialize current sense and TIM1 TRGO2 trigger.
  motor_driver_->enable();
  if (motor_driver_->HasFault()) {
    Deinit();
    return false;
  }

  current_sense_.emplace(config_.current_sense_config);
  current_sense_->linkDriver(&*motor_driver_);
  if (current_sense_->init() != 0) {
    Deinit();
    return false;
  }
  if (current_sense_->ConfigureTimerTrigger(
          motor_driver_->timer_handle(), config_.adc_trigger_compare_unit,
          config_.adc_trigger_delay_ns) != StAdcCurrentSense::Error::kOk) {
    Deinit();
    return false;
  }

  // 4. Configure and initialize SimpleFOC BLDCMotor.
  // In open-loop voltage mode, leave phase_resistance unset so SimpleFOC
  // applies `voltage_limit` directly as Uq rather than scaling by R.
  if (config_.controller == MotionControlType::velocity_openloop ||
      config_.controller == MotionControlType::angle_openloop) {
    motor_.emplace(static_cast<int>(config_.pole_pairs));
  } else {
    motor_.emplace(static_cast<int>(config_.pole_pairs),
                   config_.phase_resistance, config_.kv_rating);
  }
  motor_->linkDriver(&*motor_driver_);
  motor_->linkSensor(angle_sensor_);
  if (config_.link_current_sense) {
    motor_->linkCurrentSense(&*current_sense_);
  }
  motor_->controller = config_.controller;
  motor_->torque_controller = config_.torque_controller;
  motor_->voltage_limit = config_.voltage_limit;
  motor_->current_limit = config_.current_limit;
  motor_->velocity_limit = config_.max_velocity_rad_s;
  motor_->voltage_sensor_align = config_.voltage_sensor_align;
  motor_->foc_modulation = FOCModulationType::SpaceVectorPWM;

  if (motor_->init() != 1) {
    Deinit();
    return false;
  }
  if (motor_->initFOC() != 1) {
    Deinit();
    return false;
  }

  isr_count_.store(0U, std::memory_order_relaxed);
  isr_cycles_sum_ = 0U;
  isr_cycles_max_.store(0U, std::memory_order_relaxed);
  if (config_.target_ramp_ms > 0.0f &&
      config_.motor_driver_config.pwm_freq > 0U) {
    const float steps_f =
        config_.target_ramp_ms * 1e-3f *
        static_cast<float>(config_.motor_driver_config.pwm_freq);
    ramp_steps_ = static_cast<uint32_t>(std::lround(steps_f));
  } else {
    ramp_steps_ = 0U;
  }
  inv_ramp_steps_ =
      (ramp_steps_ > 1U) ? (1.0f / static_cast<float>(ramp_steps_)) : 0.0f;
  const float initial_target = target_.load(std::memory_order_relaxed);
  last_target_ = initial_target;
  effective_target_ = initial_target;
  target_step_ = 0.0f;
  steps_remaining_ = 0U;
  last_ia_ = 0.0f;
  last_ib_ = 0.0f;
  last_ic_ = 0.0f;
  initialized_ = true;

  // 5. Hook DoFOC() into the current-sense ADC injected conversion interrupt.
  if (current_sense_->EnableInterrupt(&FocLoop::AdcIrqTrampoline, this,
                                      config_.adc_irq_priority) !=
      StAdcCurrentSense::Error::kOk) {
    Deinit();
    return false;
  }

  return true;
}

void FocLoop::Deinit() {
  if (current_sense_) {
    current_sense_->DisableInterrupt();
  }
  if (motor_) {
    motor_->disable();
    motor_.reset();
  }
  if (current_sense_) {
    current_sense_.reset();
  }
  angle_sensor_ = nullptr;
  as5048a_sensor_.reset();
  as5047p_sensor_.reset();
  if (motor_driver_) {
    motor_driver_->disable();
    motor_driver_.reset();
  }
  initialized_ = false;
}

void FocLoop::DoFOC() {
  if (!initialized_) {
    return;
  }

  const uint32_t t0 = DWT->CYCCNT;

  isr_count_.fetch_add(1U, std::memory_order_relaxed);

  const PhaseCurrent_s currents = current_sense_->getPhaseCurrents();
  last_ia_ = currents.a;
  last_ib_ = currents.b;
  last_ic_ = currents.c;

  const float cmd = target_.load(std::memory_order_relaxed);
  if (cmd != last_target_) {
    last_target_ = cmd;
    if (ramp_steps_ <= 1U) {
      effective_target_ = cmd;
      steps_remaining_ = 0U;
    } else {
      target_step_ = (cmd - effective_target_) * inv_ramp_steps_;
      steps_remaining_ = ramp_steps_;
    }
  }

  if (steps_remaining_ > 0U) {
    if (--steps_remaining_ == 0U) {
      effective_target_ = last_target_;
    } else {
      effective_target_ += target_step_;
    }
  }

  motor_->loopFOC();
  motor_->move(effective_target_);

  const uint32_t elapsed = DWT->CYCCNT - t0;
  isr_cycles_sum_ += elapsed;
  uint32_t prev_max = isr_cycles_max_.load(std::memory_order_relaxed);
  if (elapsed > prev_max) {
    isr_cycles_max_.store(elapsed, std::memory_order_relaxed);
  }
}

uint64_t FocLoop::isr_cycles_sum() const {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const uint64_t sum = isr_cycles_sum_;
  __set_PRIMASK(primask);
  return sum;
}

void FocLoop::ResetIsrTiming() {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  isr_cycles_sum_ = 0U;
  isr_cycles_max_.store(0U, std::memory_order_relaxed);
  isr_count_.store(0U, std::memory_order_relaxed);
  __set_PRIMASK(primask);
}

PhaseCurrent_s FocLoop::last_currents() const {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const PhaseCurrent_s c{last_ia_, last_ib_, last_ic_};
  __set_PRIMASK(primask);
  return c;
}

void FocLoop::AdcIrqTrampoline(void* ctx) {
  if (ctx != nullptr) {
    static_cast<FocLoop*>(ctx)->DoFOC();
  }
}

}  // namespace stfoc
