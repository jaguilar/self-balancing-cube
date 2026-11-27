#include "foc_loop.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <optional>

#include "CppUTest/TestHarness.h"
#include "board_config.h"
#include "communication/SimpleFOCDebug.h"
#include "drv8304h_hil_config.h"
#include "stm32_hal.h"

namespace {

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
constexpr float kTargetRpm = 120.0f;
constexpr float kTargetRadPerSec = kTargetRpm * kTwoPi / 60.0f;
constexpr float kSupplyVoltageV = 12.0f;
// Windings + R_ds(on) are ~60-80 mOhm; 0.5 V keeps peak phase current ~2-3 A
// while staying above the ~0.32 V back-EMF at 120 RPM (380 KV).
constexpr float kOpenLoopVoltageLimitV = 0.5f;
constexpr float kSensorAlignVoltageV = 0.5f;
constexpr uint32_t kRampUpMs = 500U;
constexpr uint32_t kMeasureMs = 1000U;
constexpr uint32_t kSamplePeriodMs = 50U;

float ReadAngleAtomic(Sensor* sensor) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const float angle = sensor->getAngle();
  __set_PRIMASK(primask);
  return angle;
}

}  // namespace

TEST_GROUP(FocLoopOpenLoop) {
  std::optional<stfoc::FocLoop> foc_loop;

  void setup() override {
    SimpleFOCDebug::enable(&Serial);

    stfoc::FocLoopConfig cfg = stfoc::FocLoop::DefaultConfig();
    cfg.motor_driver_config = board::GetMotorDriverConfig(25000U);
    cfg.current_sense_config = board::drv8304h_hil::GetCurrentSenseConfig();
    cfg.angle_sensor_config = board::drv8304h_hil::GetAs5048aSensorConfig();
    cfg.pole_pairs = 11;
    cfg.phase_resistance = 0.07f;
    cfg.kv_rating = 380.0f;
    cfg.supply_voltage = kSupplyVoltageV;
    cfg.voltage_limit = kOpenLoopVoltageLimitV;
    cfg.voltage_sensor_align = kSensorAlignVoltageV;
    cfg.controller = MotionControlType::velocity_openloop;
    cfg.link_current_sense = false;

    foc_loop.emplace(cfg);
  }

  void teardown() override {
    if (foc_loop) {
      foc_loop->SetTarget(0.0f);
      foc_loop->Deinit();
      foc_loop.reset();
    }
    SimpleFOCDebug::enable(nullptr);
  }
};

TEST(FocLoopOpenLoop, SpinsAt120RpmWithActiveCurrentSense) {
  CHECK_TRUE(foc_loop->Init());
  auto* cs = foc_loop->current_sense();
  CHECK_TRUE(cs != nullptr);
  std::printf(
      "[FocLoopOpenLoop] post-init: has_fault=%d pin_active=%d "
      "offset_a=%.3fV offset_b=%.3fV raw_a=%u raw_b=%u isr=%lu\r\n",
      foc_loop->motor_driver()->HasFault() ? 1 : 0,
      foc_loop->motor_driver()->IsFaultPinActive() ? 1 : 0,
      static_cast<double>(cs->offset_ia), static_cast<double>(cs->offset_ib),
      static_cast<unsigned>(cs->raw_a()), static_cast<unsigned>(cs->raw_b()),
      static_cast<unsigned long>(foc_loop->isr_count()));
  CHECK_FALSE(foc_loop->motor_driver()->HasFault());
  CHECK_TRUE(cs->initialized);
  // Fail immediately if current-sense alignment swapped phases or inverted
  // gains.
  LONGS_EQUAL(0, cs->pinA);
  LONGS_EQUAL(1, cs->pinB);
  CHECK_TRUE(cs->gain_a > 0.0f);
  CHECK_TRUE(cs->gain_b > 0.0f);
  // DRV8304H CAL pin shorts sense amp inputs to VREF/2 (~1.25 V for 2.5 V
  // VREF or ~1.65 V for 3.3 V VREF).
  CHECK_TRUE(cs->offset_ia > 0.8f && cs->offset_ia < 2.5f);
  CHECK_TRUE(cs->offset_ib > 0.8f && cs->offset_ib < 2.5f);

  // Ramp smoothly from 0 to 120 RPM so the flywheel accelerates synchronously
  // with the open-loop rotating field.
  constexpr uint32_t kRampSteps = 50U;
  constexpr uint32_t kRampStepMs = 20U;
  for (uint32_t i = 1; i <= kRampSteps; ++i) {
    const float frac =
        static_cast<float>(i) / static_cast<float>(kRampSteps);
    foc_loop->SetTarget(kTargetRadPerSec * frac);
    HAL_Delay(kRampStepMs);
  }
  HAL_Delay(kRampUpMs);

  const uint32_t start_ms = HAL_GetTick();
  const uint32_t start_isr = foc_loop->isr_count();
  const float start_angle = ReadAngleAtomic(foc_loop->angle_sensor());

  float max_abs_ia = 0.0f;
  float max_abs_ib = 0.0f;
  uint16_t last_raw_a = 0U;
  uint16_t last_raw_b = 0U;

  while ((HAL_GetTick() - start_ms) < kMeasureMs) {
    HAL_Delay(kSamplePeriodMs);
    const PhaseCurrent_s c = foc_loop->last_currents();
    last_raw_a = cs->raw_a();
    last_raw_b = cs->raw_b();
    max_abs_ia = std::fmax(max_abs_ia, std::fabs(c.a));
    max_abs_ib = std::fmax(max_abs_ib, std::fabs(c.b));
    DOUBLES_EQUAL(0.0, static_cast<double>(c.a + c.b + c.c), 1e-4);
  }

  const uint32_t end_ms = HAL_GetTick();
  const uint32_t end_isr = foc_loop->isr_count();
  const float end_angle = ReadAngleAtomic(foc_loop->angle_sensor());

  foc_loop->SetTarget(0.0f);

  const float dt_s = static_cast<float>(end_ms - start_ms) * 1e-3f;
  const float measured_rad_s = std::fabs((end_angle - start_angle) / dt_s);
  const float measured_rpm = measured_rad_s * 60.0f / kTwoPi;
  const float isr_rate_hz = static_cast<float>(end_isr - start_isr) / dt_s;

  auto* m = foc_loop->motor();
  std::printf(
      "[FocLoopOpenLoop] target=%.1f RPM measured=%.2f RPM (%.2f rad/s) "
      "isr_rate=%.0f Hz en=%d shaft=%.2f uq=%.2fV "
      "Ua=%.2f Ub=%.2f Uc=%.2f\r\n"
      "  TIM1: CR1=0x%04lX CCER=0x%04lX BDTR=0x%04lX ARR=%lu "
      "CCR1=%lu CCR2=%lu CCR4=%lu GPIOB_ODR=0x%04lX GPIOB_IDR=0x%04lX\r\n"
      "  CS: offset_a=%.3fV offset_b=%.3fV "
      "raw_a=%u raw_b=%u max|ia|=%.2fA max|ib|=%.2fA fault=%d\r\n",
      static_cast<double>(kTargetRpm), static_cast<double>(measured_rpm),
      static_cast<double>(measured_rad_s), static_cast<double>(isr_rate_hz),
      static_cast<int>(m->enabled), static_cast<double>(m->shaft_angle),
      static_cast<double>(m->voltage.q), static_cast<double>(m->Ua),
      static_cast<double>(m->Ub), static_cast<double>(m->Uc),
      static_cast<unsigned long>(TIM1->CR1),
      static_cast<unsigned long>(TIM1->CCER),
      static_cast<unsigned long>(TIM1->BDTR),
      static_cast<unsigned long>(TIM1->ARR),
      static_cast<unsigned long>(TIM1->CCR1),
      static_cast<unsigned long>(TIM1->CCR2),
      static_cast<unsigned long>(TIM1->CCR4),
      static_cast<unsigned long>(GPIOB->ODR),
      static_cast<unsigned long>(GPIOB->IDR),
      static_cast<double>(cs->offset_ia), static_cast<double>(cs->offset_ib),
      static_cast<unsigned>(last_raw_a), static_cast<unsigned>(last_raw_b),
      static_cast<double>(max_abs_ia), static_cast<double>(max_abs_ib),
      foc_loop->motor_driver()->HasFault() ? 1 : 0);

  const bool has_fault = foc_loop->motor_driver()->HasFault();
  foc_loop->Deinit();

  CHECK_FALSE(has_fault);
  // ADC ISR should be firing at the 25 kHz PWM frequency.
  CHECK_TRUE(isr_rate_hz > 20000.0f && isr_rate_hz < 30000.0f);
  // Wheel should be tracking the 120 RPM open-loop target.
  DOUBLES_EQUAL(static_cast<double>(kTargetRpm),
                static_cast<double>(measured_rpm), 20.0);
  // Current sense ADCs should be active and not railed.
  CHECK_TRUE(last_raw_a > 100U && last_raw_a < 4000U);
  CHECK_TRUE(last_raw_b > 100U && last_raw_b < 4000U);
}

TEST_GROUP(FocLoopTorqueControl) {
  std::optional<stfoc::FocLoop> foc_loop;

  void setup() override {
    SimpleFOCDebug::enable(&Serial);

    stfoc::FocLoopConfig cfg = stfoc::FocLoop::DefaultConfig();
    cfg.motor_driver_config = board::GetMotorDriverConfig(25000U);
    cfg.current_sense_config = board::drv8304h_hil::GetCurrentSenseConfig();
    cfg.angle_sensor_config = board::drv8304h_hil::GetAs5048aSensorConfig();
    cfg.pole_pairs = 11;
    cfg.phase_resistance = 0.07f;
    cfg.kv_rating = 380.0f;
    cfg.supply_voltage = kSupplyVoltageV;
    // 380 KV line-to-line -> ~0.85 V phase amplitude back-EMF at 500 RPM,
    // naturally capping speed without chopping torque.
    cfg.voltage_limit = 0.85f;
    cfg.voltage_sensor_align = 0.7f;
    cfg.current_limit = 2.0f;
    cfg.controller = MotionControlType::torque;
    cfg.torque_controller = TorqueControlType::foc_current;
    cfg.link_current_sense = true;

    foc_loop.emplace(cfg);
  }

  void teardown() override {
    if (foc_loop) {
      foc_loop->SetTarget(0.0f);
      foc_loop->Deinit();
      foc_loop.reset();
    }
    SimpleFOCDebug::enable(nullptr);
  }
};

TEST(FocLoopTorqueControl, SweepsCurrentTargetsOver5Seconds) {
  CHECK_TRUE(foc_loop->Init());
  auto* cs = foc_loop->current_sense();
  auto* m = foc_loop->motor();
  CHECK_TRUE(cs != nullptr);
  CHECK_TRUE(m != nullptr);
  std::printf(
      "[FocLoopTorque] post-init: zero_el=%.3f dir=%d "
      "pins=(%d,%d,%d) gains=(%.1f,%.1f,%.1f) "
      "offsets=(%.3f,%.3f,%.3f)V fault=%d\r\n",
      static_cast<double>(m->zero_electric_angle),
      static_cast<int>(m->sensor_direction), cs->pinA, cs->pinB, cs->pinC,
      static_cast<double>(cs->gain_a), static_cast<double>(cs->gain_b),
      static_cast<double>(cs->gain_c), static_cast<double>(cs->offset_ia),
      static_cast<double>(cs->offset_ib), static_cast<double>(cs->offset_ic),
      foc_loop->motor_driver()->HasFault() ? 1 : 0);
  CHECK_FALSE(foc_loop->motor_driver()->HasFault());
  CHECK_TRUE(cs->initialized);
  // Fail immediately if current-sense alignment swapped phases or inverted
  // gains.
  LONGS_EQUAL(0, cs->pinA);
  LONGS_EQUAL(1, cs->pinB);
  CHECK_TRUE(cs->gain_a > 0.0f);
  CHECK_TRUE(cs->gain_b > 0.0f);

  constexpr uint32_t kSweepDurationMs = 5000U;
  constexpr uint32_t kStepPeriodMs = 50U;
  constexpr uint32_t kLogIntervalMs = 500U;
  constexpr float kMaxCurrentA = 2.0f;

  const uint32_t start_ms = HAL_GetTick();
  const uint32_t start_isr = foc_loop->isr_count();
  uint32_t last_log_ms = start_ms;

  float max_abs_iq = 0.0f;
  float max_abs_vel = 0.0f;
  uint16_t last_raw_a = 0U;
  uint16_t last_raw_b = 0U;

  while (true) {
    const uint32_t now_ms = HAL_GetTick();
    const uint32_t elapsed_ms = now_ms - start_ms;
    if (elapsed_ms >= kSweepDurationMs) {
      break;
    }

    // Smooth sinusoidal sweep across [-2 A, +2 A] over 5 seconds.
    const float phase =
        kTwoPi * static_cast<float>(elapsed_ms) / static_cast<float>(kSweepDurationMs);
    const float target_iq = kMaxCurrentA * std::sin(phase);
    foc_loop->SetTarget(target_iq);

    HAL_Delay(kStepPeriodMs);

    const PhaseCurrent_s c = foc_loop->last_currents();
    last_raw_a = cs->raw_a();
    last_raw_b = cs->raw_b();
    const float iq = m->current.q;
    const float id = m->current.d;
    const float vel = m->shaft_velocity;
    max_abs_iq = std::fmax(max_abs_iq, std::fabs(iq));
    max_abs_vel = std::fmax(max_abs_vel, std::fabs(vel));
    DOUBLES_EQUAL(0.0, static_cast<double>(c.a + c.b + c.c), 1e-4);

    if ((HAL_GetTick() - last_log_ms) >= kLogIntervalMs) {
      last_log_ms = HAL_GetTick();
      std::printf(
          "[FocLoopTorque] t=%lums target=%.2fA iq=%.2fA id=%.2fA "
          "uq=%.2fV ud=%.2fV vel=%.1frad/s raw=(%u,%u) fault=%d\r\n",
          static_cast<unsigned long>(elapsed_ms),
          static_cast<double>(target_iq), static_cast<double>(iq),
          static_cast<double>(id), static_cast<double>(m->voltage.q),
          static_cast<double>(m->voltage.d), static_cast<double>(vel),
          static_cast<unsigned>(last_raw_a), static_cast<unsigned>(last_raw_b),
          foc_loop->motor_driver()->HasFault() ? 1 : 0);
    }
  }

  foc_loop->SetTarget(0.0f);

  const uint32_t end_ms = HAL_GetTick();
  const uint32_t end_isr = foc_loop->isr_count();
  const float dt_s = static_cast<float>(end_ms - start_ms) * 1e-3f;
  const float isr_rate_hz = static_cast<float>(end_isr - start_isr) / dt_s;

  std::printf(
      "[FocLoopTorque] done: dt=%.2fs isr_rate=%.0fHz "
      "max|iq|=%.2fA max|vel|=%.1frad/s fault=%d\r\n",
      static_cast<double>(dt_s), static_cast<double>(isr_rate_hz),
      static_cast<double>(max_abs_iq), static_cast<double>(max_abs_vel),
      foc_loop->motor_driver()->HasFault() ? 1 : 0);

  const bool has_fault = foc_loop->motor_driver()->HasFault();
  foc_loop->Deinit();

  CHECK_FALSE(has_fault);
  CHECK_TRUE(isr_rate_hz > 20000.0f && isr_rate_hz < 30000.0f);
  CHECK_TRUE(last_raw_a > 100U && last_raw_a < 4000U);
  CHECK_TRUE(last_raw_b > 100U && last_raw_b < 4000U);
  CHECK_TRUE(max_abs_iq > 0.2f);
  CHECK_TRUE(max_abs_vel < 65.0f);
}
