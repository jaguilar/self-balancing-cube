#include "current_sense.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>

#include "CppUTest/TestHarness.h"
#include "board_config.h"
#include "motor_driver.h"
#include "stm32_hal.h"

namespace {

constexpr uint32_t kPwmFreqHz = 25000U;
constexpr uint32_t kStimPwmFreqHz = 100000U;
constexpr uint32_t kRcSettleMs = 150U;
constexpr float kVref = 3.3f;
constexpr float kShuntOhms = 0.005f;

// Harness drives drv_sense_a (PA1 / ADC1_IN1) from PC12 (TIM15_CH1, AF2)
// through an external 10k + 100nF RC low-pass filter.
class Tim15StimHarness {
 public:
  void Init() {
    HAL_RCC_GPIOC_EnableClock();
    HAL_RCC_TIM15_EnableClock();

    hal_gpio_config_t gpio_cfg{};
    gpio_cfg.mode = HAL_GPIO_MODE_ALTERNATE;
    gpio_cfg.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
    gpio_cfg.pull = HAL_GPIO_PULL_NO;
    gpio_cfg.speed = HAL_GPIO_SPEED_FREQ_HIGH;
    gpio_cfg.alternate = HAL_GPIO_AF_2;
    CHECK_EQUAL(HAL_OK, HAL_GPIO_Init(HAL_GPIOC, HAL_GPIO_PIN_12, &gpio_cfg));

    CHECK_EQUAL(HAL_OK, HAL_TIM_Init(&htim15_, HAL_TIM15));

    const uint32_t tim_clk_hz = HAL_RCC_GetPCLK2Freq();
    period_ = tim_clk_hz / kStimPwmFreqHz;
    CHECK_TRUE(period_ > 10U);

    hal_tim_config_t tim_cfg{};
    tim_cfg.prescaler = 0U;
    tim_cfg.counter_mode = HAL_TIM_COUNTER_UP;
    tim_cfg.period = period_ - 1U;
    tim_cfg.repetition_counter = 0U;
    tim_cfg.clock_sel.clock_source = HAL_TIM_CLK_INTERNAL;
    CHECK_EQUAL(HAL_OK, HAL_TIM_SetConfig(&htim15_, &tim_cfg));
    CHECK_EQUAL(HAL_OK, HAL_TIM_EnableAutoReloadPreload(&htim15_));

    hal_tim_oc_channel_config_t oc_ch_cfg{};
    oc_ch_cfg.polarity = HAL_TIM_OC_HIGH;
    oc_ch_cfg.idle_state = HAL_TIM_OC_IDLE_STATE_RESET;
    oc_ch_cfg.override_state = HAL_TIM_OC_OVERRIDE_RESET;
    oc_ch_cfg.break_mode = HAL_TIM_OC_BREAKMODE_IMMEDIATE;
    CHECK_EQUAL(HAL_OK, HAL_TIM_OC_SetConfigChannel(&htim15_, HAL_TIM_CHANNEL_1,
                                                    &oc_ch_cfg));

    hal_tim_oc_compare_unit_config_t oc_unit_cfg{};
    oc_unit_cfg.mode = HAL_TIM_OC_PWM1;
    oc_unit_cfg.pulse = period_ / 2U;
    CHECK_EQUAL(HAL_OK,
                HAL_TIM_OC_SetConfigCompareUnit(
                    &htim15_, HAL_TIM_OC_COMPARE_UNIT_1, &oc_unit_cfg));
    CHECK_EQUAL(HAL_OK, HAL_TIM_OC_EnableComparePreload(
                            &htim15_, HAL_TIM_OC_COMPARE_UNIT_1));

    CHECK_EQUAL(HAL_OK, HAL_TIM_Start(&htim15_));
    CHECK_EQUAL(HAL_OK, HAL_TIM_OC_StartChannel(&htim15_, HAL_TIM_CHANNEL_1));
    initialized_ = true;
  }

  void Deinit() {
    if (!initialized_) {
      return;
    }
    (void)HAL_TIM_OC_StopChannel(&htim15_, HAL_TIM_CHANNEL_1);
    (void)HAL_TIM_Stop(&htim15_);
    (void)HAL_TIM_DeInit(&htim15_);
    HAL_GPIO_DeInit(HAL_GPIOC, HAL_GPIO_PIN_12);
    initialized_ = false;
  }

  void SetDuty(float duty, uint32_t settle_ms = kRcSettleMs) {
    const uint32_t pulse = static_cast<uint32_t>(
        std::lround(duty * static_cast<float>(period_)));
    CHECK_EQUAL(HAL_OK, HAL_TIM_OC_SetCompareUnitPulse(
                            &htim15_, HAL_TIM_OC_COMPARE_UNIT_1, pulse));
    HAL_Delay(settle_ms);
  }

  static void SetCalModeCallback(bool enable, void* ctx) {
    if (enable && ctx != nullptr) {
      static_cast<Tim15StimHarness*>(ctx)->SetDuty(0.50f, 100U);
    }
  }

 private:
  hal_tim_handle_t htim15_{};
  uint32_t period_{0};
  bool initialized_{false};
};

}  // namespace

TEST_GROUP(CurrentSense) {
  Tim15StimHarness harness;
  std::optional<stfoc::StTimerMotorDriver> driver;

  void setup() override {
    harness.Init();
    driver.emplace(board::GetMotorDriverConfig(kPwmFreqHz));
    driver->voltage_power_supply = 12.0f;
    driver->voltage_limit = 12.0f;
    LONGS_EQUAL(1, driver->init());
  }

  void teardown() override {
    driver.reset();
    harness.Deinit();
  }
};

TEST(CurrentSense, PhaseACurrentMatchesConfiguredGain20VV) {
  constexpr float kGainVV = 20.0f;
  auto cfg = board::GetCurrentSenseConfig(kShuntOhms, kGainVV);
  cfg.v_ref = kVref;
  cfg.set_cal_mode = &Tim15StimHarness::SetCalModeCallback;
  cfg.cal_ctx = &harness;

  stfoc::StAdcCurrentSense sensor(cfg);
  LONGS_EQUAL(0, sensor.init());
  CHECK_TRUE(sensor.last_error() == stfoc::StAdcCurrentSense::Error::kOk);
  CHECK_TRUE(sensor.initialized);
  DOUBLES_EQUAL(kVref * 0.5f, sensor.offset_ia, 0.1);

  // Trigger 500 ns after the center of low-side MOSFET on-time (CNT = 0).
  CHECK_TRUE(sensor.ConfigureTimerTrigger(
                 driver->timer_handle(), HAL_TIM_OC_COMPARE_UNIT_3, 500U) ==
             stfoc::StAdcCurrentSense::Error::kOk);

  // Sweep duty cycles -> voltages on PA1:
  //   0.50 -> 1.65 V ->   0.0 A
  //   0.75 -> 2.475 V -> +8.25 A (at 0.005 Ohm * 20 V/V = 0.1 V/A)
  //   0.25 -> 0.825 V -> -8.25 A
  constexpr float kDuties[] = {0.50f, 0.75f, 0.25f};
  for (float duty : kDuties) {
    harness.SetDuty(duty);
    const PhaseCurrent_s currents = sensor.getPhaseCurrents();
    const float expected_ia =
        ((duty - 0.5f) * kVref) / (kShuntOhms * kGainVV);

    std::printf(
        "[CurrentSense 20V/V] offset_ia=%.4fV duty=%.2f raw_a=%u ia=%.3fA "
        "(exp=%.3fA) ib=%.3fA ic=%.3fA sum=%.5fA\r\n",
        static_cast<double>(sensor.offset_ia), static_cast<double>(duty),
        static_cast<unsigned>(sensor.raw_a()), static_cast<double>(currents.a),
        static_cast<double>(expected_ia), static_cast<double>(currents.b),
        static_cast<double>(currents.c),
        static_cast<double>(currents.a + currents.b + currents.c));

    // Allow 0.25 A tolerance (~25 mV) after zero-offset CAL.
    DOUBLES_EQUAL(expected_ia, currents.a, 0.25);
    DOUBLES_EQUAL(0.0, currents.a + currents.b + currents.c, 1e-4);
  }
}

TEST(CurrentSense, PhaseACurrentScalesWithGain10VV) {
  constexpr float kGainVV = 10.0f;
  auto cfg = board::GetCurrentSenseConfig(kShuntOhms, kGainVV);
  cfg.v_ref = kVref;
  cfg.set_cal_mode = &Tim15StimHarness::SetCalModeCallback;
  cfg.cal_ctx = &harness;

  stfoc::StAdcCurrentSense sensor(cfg);
  LONGS_EQUAL(0, sensor.init());
  CHECK_TRUE(sensor.last_error() == stfoc::StAdcCurrentSense::Error::kOk);
  DOUBLES_EQUAL(kVref * 0.5f, sensor.offset_ia, 0.1);
  CHECK_TRUE(sensor.ConfigureTimerTrigger(
                 driver->timer_handle(), HAL_TIM_OC_COMPARE_UNIT_3, 1000U) ==
             stfoc::StAdcCurrentSense::Error::kOk);

  // At 10 V/V and 0.005 Ohm (0.05 V/A):
  //   0.75 -> 2.475 V -> +16.5 A
  //   0.25 -> 0.825 V -> -16.5 A
  constexpr float kDuties[] = {0.75f, 0.25f};
  for (float duty : kDuties) {
    harness.SetDuty(duty);
    const PhaseCurrent_s currents = sensor.getPhaseCurrents();
    const float expected_ia =
        ((duty - 0.5f) * kVref) / (kShuntOhms * kGainVV);

    std::printf(
        "[CurrentSense 10V/V] offset_ia=%.4fV duty=%.2f raw_a=%u ia=%.3fA "
        "(exp=%.3fA) sum=%.5fA\r\n",
        static_cast<double>(sensor.offset_ia), static_cast<double>(duty),
        static_cast<unsigned>(sensor.raw_a()), static_cast<double>(currents.a),
        static_cast<double>(expected_ia),
        static_cast<double>(currents.a + currents.b + currents.c));

    // Allow 0.50 A tolerance (~25 mV at 0.05 V/A) after zero-offset CAL.
    DOUBLES_EQUAL(expected_ia, currents.a, 0.50);
    DOUBLES_EQUAL(0.0, currents.a + currents.b + currents.c, 1e-4);
  }
}
