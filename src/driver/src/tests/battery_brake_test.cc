#include "battery_brake.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "CppUTest/TestHarness.h"
#include "current_sense.h"
#include "motor_driver.h"
#include "stm32_hal.h"
#include "stm32c5xx_ll_adc.h"

namespace {

constexpr uint32_t kStimPwmFreqHz = 100000U;
constexpr uint32_t kRcSettleMs = 80U;
constexpr float kBrakeThresholdV = 22.0f;

// Harness drives vm_sense (PB0 / COMP1_INP2 / ADC2_IN6) from PB14
// (TIM12_CH1, AF2) through an external 10k + 100nF RC low-pass filter.
class Tim12StimHarness {
 public:
  void Init() {
    HAL_RCC_GPIOB_EnableClock();
    HAL_RCC_TIM12_EnableClock();

    hal_gpio_config_t gpio_cfg{};
    gpio_cfg.mode = HAL_GPIO_MODE_ALTERNATE;
    gpio_cfg.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
    gpio_cfg.pull = HAL_GPIO_PULL_NO;
    gpio_cfg.speed = HAL_GPIO_SPEED_FREQ_HIGH;
    gpio_cfg.alternate = HAL_GPIO_AF_2;
    CHECK_EQUAL(HAL_OK, HAL_GPIO_Init(HAL_GPIOB, HAL_GPIO_PIN_14, &gpio_cfg));

    CHECK_EQUAL(HAL_OK, HAL_TIM_Init(&htim12_, HAL_TIM12));

    const uint32_t tim_clk_hz = HAL_RCC_GetPCLK1Freq();
    period_ = tim_clk_hz / kStimPwmFreqHz;
    CHECK_TRUE(period_ > 10U);

    hal_tim_config_t tim_cfg{};
    tim_cfg.prescaler = 0U;
    tim_cfg.counter_mode = HAL_TIM_COUNTER_UP;
    tim_cfg.period = period_ - 1U;
    tim_cfg.repetition_counter = 0U;
    tim_cfg.clock_sel.clock_source = HAL_TIM_CLK_INTERNAL;
    CHECK_EQUAL(HAL_OK, HAL_TIM_SetConfig(&htim12_, &tim_cfg));
    CHECK_EQUAL(HAL_OK, HAL_TIM_EnableAutoReloadPreload(&htim12_));

    hal_tim_oc_channel_config_t oc_ch_cfg{};
    oc_ch_cfg.polarity = HAL_TIM_OC_HIGH;
    oc_ch_cfg.idle_state = HAL_TIM_OC_IDLE_STATE_RESET;
    oc_ch_cfg.override_state = HAL_TIM_OC_OVERRIDE_RESET;
    oc_ch_cfg.break_mode = HAL_TIM_OC_BREAKMODE_IMMEDIATE;
    CHECK_EQUAL(HAL_OK, HAL_TIM_OC_SetConfigChannel(&htim12_, HAL_TIM_CHANNEL_1,
                                                    &oc_ch_cfg));

    hal_tim_oc_compare_unit_config_t oc_unit_cfg{};
    oc_unit_cfg.mode = HAL_TIM_OC_PWM1;
    oc_unit_cfg.pulse = 0U;
    CHECK_EQUAL(HAL_OK,
                HAL_TIM_OC_SetConfigCompareUnit(
                    &htim12_, HAL_TIM_OC_COMPARE_UNIT_1, &oc_unit_cfg));
    CHECK_EQUAL(HAL_OK, HAL_TIM_OC_EnableComparePreload(
                            &htim12_, HAL_TIM_OC_COMPARE_UNIT_1));

    CHECK_EQUAL(HAL_OK, HAL_TIM_Start(&htim12_));
    CHECK_EQUAL(HAL_OK, HAL_TIM_OC_StartChannel(&htim12_, HAL_TIM_CHANNEL_1));
    initialized_ = true;
  }

  void Deinit() {
    if (!initialized_) {
      return;
    }
    (void)HAL_TIM_OC_StopChannel(&htim12_, HAL_TIM_CHANNEL_1);
    (void)HAL_TIM_Stop(&htim12_);
    (void)HAL_TIM_DeInit(&htim12_);
    HAL_GPIO_DeInit(HAL_GPIOB, HAL_GPIO_PIN_14);
    initialized_ = false;
  }

  void SetDuty(float duty, uint32_t settle_ms = kRcSettleMs) {
    const uint32_t pulse = static_cast<uint32_t>(
        std::lround(duty * static_cast<float>(period_)));
    CHECK_EQUAL(HAL_OK, HAL_TIM_OC_SetCompareUnitPulse(
                            &htim12_, HAL_TIM_OC_COMPARE_UNIT_1, pulse));
    if (settle_ms > 0U) {
      HAL_Delay(settle_ms);
    }
  }

  void SetPinVoltage(float v_pin, float v_dda,
                     uint32_t settle_ms = kRcSettleMs) {
    SetDuty(v_pin / v_dda, settle_ms);
  }

 private:
  hal_tim_handle_t htim12_{};
  uint32_t period_{0};
  bool initialized_{false};
};

stfoc::StBatteryBrakeConfig NucleoTestConfig(
    float brake_threshold_v = kBrakeThresholdV) {
  auto cfg = stfoc::DefaultBatteryBrakeConfig(brake_threshold_v);
  // Leave PA3 untouched on Nucleo-C562RE (connected to ST-Link VCP RX).
  cfg.enable_output_pin = false;
  return cfg;
}

}  // namespace

TEST_GROUP(BatteryBrake) {
  Tim12StimHarness harness;

  void setup() override { harness.Init(); }

  void teardown() override { harness.Deinit(); }
};

TEST(BatteryBrake, ComparatorRisingTripTimingAndDynamicThreshold) {
  stfoc::StBatteryBrake battery(NucleoTestConfig(kBrakeThresholdV));
  CHECK_TRUE(battery.Init() == stfoc::StBatteryBrake::Error::kOk);
  CHECK_TRUE(battery.initialized());

  const float v_dda = battery.v_dda_v();
  const float v_thresh_pin = battery.brake_pin_threshold_v();
  std::printf(
      "[BatteryBrake] Calibrated V_DDA=%.4fV divider=%.2fx thresh=%.2fV "
      "(pin=%.4fV)\r\n",
      static_cast<double>(v_dda), static_cast<double>(battery.DividerScale()),
      static_cast<double>(battery.brake_threshold_v()),
      static_cast<double>(v_thresh_pin));
  DOUBLES_EQUAL(3.3, v_dda, 0.2);

  // 1. Step-crossing timing check: settle below DAC1 threshold, step above,
  // and verify COMP1_OUT goes high within 20 ms.
  harness.SetPinVoltage(v_thresh_pin - 0.15f, v_dda, kRcSettleMs);
  CHECK_FALSE(battery.IsBrakeActive());

  harness.SetPinVoltage(v_thresh_pin + 0.15f, v_dda, 0U);
  const uint32_t t_start = HAL_GetTick();
  bool tripped = false;
  uint32_t elapsed_ms = 0U;
  while ((HAL_GetTick() - t_start) <= 20U) {
    if (battery.IsBrakeActive()) {
      elapsed_ms = HAL_GetTick() - t_start;
      tripped = true;
      break;
    }
  }
  std::printf("[BatteryBrake] Step crossing tripped=%d in %lu ms\r\n",
              tripped ? 1 : 0, static_cast<unsigned long>(elapsed_ms));
  CHECK_TRUE(tripped);
  CHECK_TRUE(elapsed_ms <= 20U);

  // 2. Verify runtime threshold update via DAC1_CH1.
  harness.SetPinVoltage(2.35f, v_dda, kRcSettleMs);  // ~20.0 V VM
  CHECK_FALSE(battery.IsBrakeActive());              // Below 22.0 V threshold
  CHECK_TRUE(battery.SetBrakeThresholdV(18.0f) ==
             stfoc::StBatteryBrake::Error::kOk);
  HAL_Delay(2U);
  CHECK_TRUE(battery.IsBrakeActive());               // Above 18.0 V threshold
}

TEST(BatteryBrake, AdcReadingsAcrossCurveWithComparatorActive) {
  stfoc::StBatteryBrake battery(NucleoTestConfig(kBrakeThresholdV));
  CHECK_TRUE(battery.Init() == stfoc::StBatteryBrake::Error::kOk);

  const float v_dda = battery.v_dda_v();
  const float v_thresh_pin = battery.brake_pin_threshold_v();
  const float scale = battery.DividerScale();

  // Sweep across the 5S operating curve and well above the 22.0 V brake point.
  constexpr float kDuties[] = {0.45f, 0.60f, 0.70f, 0.85f, 0.92f};
  for (float duty : kDuties) {
    harness.SetDuty(duty, kRcSettleMs);

    CHECK_TRUE(battery.StartConversion() == stfoc::StBatteryBrake::Error::kOk);
    // Leave result in ADC regular DR register for a couple ms before reading.
    HAL_Delay(2U);

    const uint16_t raw = battery.ReadRaw();
    const float pin_v = battery.ReadPinVoltageV();
    const float vm_v = battery.ReadVoltageV();
    // Reading again without starting a new conversion should return the same DR.
    const float vm_v_repeat = battery.ReadVoltageV();
    const bool brake = battery.IsBrakeActive();

    const float expected_pin_v = duty * v_dda;
    const float expected_vm_v = expected_pin_v * scale;
    const bool expected_brake = (expected_pin_v > v_thresh_pin);

    std::printf(
        "[BatteryBrake Curve] duty=%.2f raw=%u pin=%.3fV (exp=%.3fV) "
        "vm=%.2fV (exp=%.2fV) brake=%d\r\n",
        static_cast<double>(duty), static_cast<unsigned>(raw),
        static_cast<double>(pin_v), static_cast<double>(expected_pin_v),
        static_cast<double>(vm_v), static_cast<double>(expected_vm_v),
        brake ? 1 : 0);

    DOUBLES_EQUAL(expected_pin_v, pin_v, 0.15);
    DOUBLES_EQUAL(expected_vm_v, vm_v, 0.15f * scale);
    DOUBLES_EQUAL(vm_v, vm_v_repeat, 1e-5);
    CHECK_EQUAL(expected_brake, brake);
  }
}

TEST(BatteryBrake, ConcurrentWithInjectedCurrentSenseOnAdc2) {
  // Start TIM1 PWM and StAdcCurrentSense (which runs TIM1 TRGO2 hardware-
  // triggered injected conversions on ADC1 and ADC2 at 25 kHz).
  stfoc::StTimerMotorDriver driver(stfoc::DefaultMotorDriverConfig(25000U));
  driver.voltage_power_supply = 18.5f;
  driver.voltage_limit = 18.5f;
  LONGS_EQUAL(1, driver.init());

  auto cs_cfg = stfoc::DefaultCurrentSenseConfig();
  stfoc::StAdcCurrentSense current_sense(cs_cfg);
  LONGS_EQUAL(0, current_sense.init());

  // Configure StBatteryBrake sharing ADC2 (and ADC1 for initial V_DDA
  // calibration) with StAdcCurrentSense, then enable the 25 kHz injected
  // trigger.
  stfoc::StBatteryBrake battery(NucleoTestConfig(kBrakeThresholdV));
  CHECK_TRUE(battery.Init(current_sense.adc_b_handle(),
                          current_sense.adc_a_handle()) ==
             stfoc::StBatteryBrake::Error::kOk);

  CHECK_TRUE(current_sense.ConfigureTimerTrigger(
                 driver.timer_handle(), HAL_TIM_OC_COMPARE_UNIT_3, 500U) ==
             stfoc::StAdcCurrentSense::Error::kOk);

  // Verify ADC2 regular conversions, DAC1_CH1, and COMP1 all work accurately
  // while ADC1/ADC2 injected conversions are firing at 25 kHz.
  const float v_dda = battery.v_dda_v();
  const float v_thresh_pin = battery.brake_pin_threshold_v();
  constexpr float kDuties[] = {0.60f, 0.88f, 0.60f};
  for (float duty : kDuties) {
    harness.SetDuty(duty, kRcSettleMs);

    CHECK_TRUE(battery.StartConversion() == stfoc::StBatteryBrake::Error::kOk);
    HAL_Delay(2U);

    const float pin_v = battery.ReadPinVoltageV();
    const bool brake = battery.IsBrakeActive();
    const float expected_pin_v = duty * v_dda;

    std::printf(
        "[BatteryBrake+CurrentSense] duty=%.2f pin=%.3fV (exp=%.3fV) brake=%d "
        "cs_raw_b=%u\r\n",
        static_cast<double>(duty), static_cast<double>(pin_v),
        static_cast<double>(expected_pin_v), brake ? 1 : 0,
        static_cast<unsigned>(current_sense.raw_b()));

    DOUBLES_EQUAL(expected_pin_v, pin_v, 0.15);
    CHECK_EQUAL(expected_pin_v > v_thresh_pin, brake);
  }

  // Deterministically force mid-conversion preemption: synchronize on TIM1
  // counting down just before the bottom-of-cycle turnaround so the TRGO2
  // injected trigger (up-counting CCR3 match, ~500 ns past CNT==0) fires
  // during ADC2's ~1.7 us regular conversion. Verify the hardware restarts the
  // aborted regular conversion after the injected conversion finishes and
  // updates DR to the new voltage (distinct from the 0.60 duty above).
  constexpr float kPreemptDuty = 0.85f;
  harness.SetDuty(kPreemptDuty, kRcSettleMs);

  const TIM_TypeDef* tim1 = reinterpret_cast<const TIM_TypeDef*>(
      static_cast<uintptr_t>(driver.timer_handle()->instance));
  ADC_TypeDef* adc2 = reinterpret_cast<ADC_TypeDef*>(
      static_cast<uintptr_t>(current_sense.adc_b_handle()->instance));

  // Wait for TIM1 counting down with CNT in [180, 240] (at 144 MHz with
  // CCR3=72, this places the TRGO2 rising edge ~1.75-2.17 us away, so after
  // ~1 us of flag-clear + StartConversion() overhead it lands squarely inside
  // the ~1.7 us regular conversion).
  const uint32_t t_sync = HAL_GetTick();
  bool synced = false;
  while ((HAL_GetTick() - t_sync) <= 10U) {
    const uint32_t cr1 = tim1->CR1;
    const uint32_t cnt = tim1->CNT;
    if (((cr1 & TIM_CR1_DIR) != 0U) && cnt >= 180U && cnt <= 240U) {
      synced = true;
      break;
    }
  }
  CHECK_TRUE(synced);

  LL_ADC_ClearFlag_EOC(adc2);
  LL_ADC_ClearFlag_JEOS(adc2);
  CHECK_TRUE(battery.StartConversion() == stfoc::StBatteryBrake::Error::kOk);

  const uint32_t t_conv = HAL_GetTick();
  bool eoc_set = false;
  while ((HAL_GetTick() - t_conv) <= 2U) {
    if (LL_ADC_IsActiveFlag_EOC(adc2) != 0U) {
      eoc_set = true;
      break;
    }
  }
  const bool inj_fired_during_reg = (LL_ADC_IsActiveFlag_JEOS(adc2) != 0U);

  const float preempt_pin_v = battery.ReadPinVoltageV();
  const float expected_preempt_pin_v = kPreemptDuty * v_dda;
  std::printf(
      "[BatteryBrake+CurrentSense Preempt] eoc=%d jeos_during_reg=%d "
      "pin=%.3fV (exp=%.3fV) cs_raw_b=%u\r\n",
      eoc_set ? 1 : 0, inj_fired_during_reg ? 1 : 0,
      static_cast<double>(preempt_pin_v),
      static_cast<double>(expected_preempt_pin_v),
      static_cast<unsigned>(current_sense.raw_b()));

  CHECK_TRUE(eoc_set);
  CHECK_TRUE(inj_fired_during_reg);
  DOUBLES_EQUAL(expected_preempt_pin_v, preempt_pin_v, 0.15);
}
