#include "as5048a_angle_sensor.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <optional>

#include "CppUTest/TestHarness.h"
#include "drv8304h_hil_config.h"
#include "stm32_hal.h"

namespace {

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
constexpr double kDegreesPerCount =
    360.0 / static_cast<double>(stfoc::As5048aAngleSensor::kCountsPerRev);
// ~2.2 degrees to detect spin start (well above ~3 LSB RMS sensor noise).
constexpr int64_t kManualSpinStartThresholdCounts = 100;
// ~0.44 degrees movement threshold to consider the wheel still moving.
constexpr int64_t kManualSpinStillThresholdCounts = 20;
constexpr uint32_t kManualSpinStartTimeoutMs = 25000U;
constexpr uint32_t kManualSpinMinDurationMs = 1000U;
constexpr uint32_t kManualSpinStillDurationMs = 300U;
constexpr uint32_t kManualSpinMaxDurationMs = 30000U;

void DumpSensorStatus(const stfoc::As5048aAngleSensor& sensor) {
  const uint16_t diag = sensor.last_diagnostics();
  std::printf(
      "AS5048A status: init=%d err=%s (%u) diag=0x%04X "
      "(AGC=%u OCF=%u COF=%u COMP_L=%u COMP_H=%u) mag=%u raw=%u\r\n",
      sensor.initialized() ? 1 : 0,
      stfoc::As5048aAngleSensor::ErrorToString(sensor.last_error()),
      static_cast<unsigned>(sensor.last_error()), static_cast<unsigned>(diag),
      static_cast<unsigned>(sensor.last_agc()),
      static_cast<unsigned>((diag >> 8) & 1U),
      static_cast<unsigned>((diag >> 9) & 1U),
      static_cast<unsigned>((diag >> 10) & 1U),
      static_cast<unsigned>((diag >> 11) & 1U),
      static_cast<unsigned>(sensor.last_magnitude()),
      static_cast<unsigned>(sensor.raw_count()));
}

void UpdateWithIrqDisabled(stfoc::As5048aAngleSensor& sensor) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  sensor.update();
  __set_PRIMASK(primask);
}

class TriggerTimerFixture {
 public:
  void Start() {
    const auto motor_cfg = board::GetMotorDriverConfig();
    if (motor_cfg.timer_instance == HAL_TIM1) {
      HAL_RCC_TIM1_EnableClock();
    } else if (motor_cfg.timer_instance == HAL_TIM8) {
      HAL_RCC_TIM8_EnableClock();
    }
    CHECK_EQUAL(HAL_OK, HAL_TIM_Init(&htim_, motor_cfg.timer_instance));

    const uint32_t tim_clk_hz = HAL_RCC_GetPCLK2Freq();
    const uint32_t arr = tim_clk_hz / (2U * motor_cfg.pwm_freq);
    hal_tim_config_t tim_cfg{};
    tim_cfg.prescaler = 0U;
    tim_cfg.counter_mode = HAL_TIM_COUNTER_CENTER_UP_DOWN;
    tim_cfg.period = arr;
    tim_cfg.repetition_counter = 0U;
    tim_cfg.clock_sel.clock_source = HAL_TIM_CLK_INTERNAL;
    CHECK_EQUAL(HAL_OK, HAL_TIM_SetConfig(&htim_, &tim_cfg));
    CHECK_EQUAL(HAL_OK, HAL_TIM_Start(&htim_));
    running_ = true;
  }

  void Stop() {
    if (!running_) {
      return;
    }
    (void)HAL_TIM_Stop(&htim_);
    (void)HAL_TIM_DeInit(&htim_);
    running_ = false;
  }

 private:
  hal_tim_handle_t htim_{};
  bool running_{false};
};

}  // namespace

TEST_GROUP(As5048aAngleSensor) {
  TriggerTimerFixture trigger_timer;
  std::optional<stfoc::As5048aAngleSensor> sensor;

  void setup() override {
    trigger_timer.Start();
    sensor.emplace(board::drv8304h_hil::GetAs5048aSensorConfig());
  }

  void teardown() override {
    sensor.reset();
    trigger_timer.Stop();
  }
};

TEST(As5048aAngleSensor, InitSucceedsWithValidMagnet) {
  sensor->init();
  DumpSensorStatus(*sensor);

  CHECK_TRUE(sensor->initialized());
  LONGS_EQUAL(static_cast<long>(stfoc::As5048aAngleSensor::Error::kNone),
              static_cast<long>(sensor->last_error()));

  HAL_Delay(2);
  UpdateWithIrqDisabled(*sensor);
  LONGS_EQUAL(static_cast<long>(stfoc::As5048aAngleSensor::Error::kNone),
              static_cast<long>(sensor->last_error()));

  const float angle = sensor->getSensorAngle();
  CHECK_TRUE(angle >= 0.0f && angle < kTwoPi);
}

TEST_GROUP(As5048aManualSpin) {
  TriggerTimerFixture trigger_timer;
  std::optional<stfoc::As5048aAngleSensor> sensor;

  void setup() override {
    trigger_timer.Start();
    sensor.emplace(board::drv8304h_hil::GetAs5048aSensorConfig());
  }

  void teardown() override {
    sensor.reset();
    trigger_timer.Stop();
  }
};

TEST(As5048aManualSpin, WaitForManualSpin) {
  sensor->init();
  DumpSensorStatus(*sensor);
  CHECK_TRUE(sensor->initialized());

  const int64_t start_count = sensor->total_count();
  std::printf("Waiting for manual flywheel spin (timeout %lu ms)...\r\n",
              static_cast<unsigned long>(kManualSpinStartTimeoutMs));

  const uint32_t wait_start_ms = HAL_GetTick();
  bool spin_started = false;

  while ((HAL_GetTick() - wait_start_ms) < kManualSpinStartTimeoutMs) {
    UpdateWithIrqDisabled(*sensor);
    LONGS_EQUAL(static_cast<long>(stfoc::As5048aAngleSensor::Error::kNone),
                static_cast<long>(sensor->last_error()));

    if (std::llabs(sensor->total_count() - start_count) >=
        kManualSpinStartThresholdCounts) {
      spin_started = true;
      break;
    }
    HAL_Delay(5);
  }
  CHECK_TRUE(spin_started);

  const uint32_t spin_start_ms = HAL_GetTick();
  uint32_t last_move_ms = spin_start_ms;
  int64_t last_move_count = sensor->total_count();
  std::printf("Spin detected! Tracking until wheel stops...\r\n");

  while ((HAL_GetTick() - spin_start_ms) < kManualSpinMaxDurationMs) {
    UpdateWithIrqDisabled(*sensor);
    LONGS_EQUAL(static_cast<long>(stfoc::As5048aAngleSensor::Error::kNone),
                static_cast<long>(sensor->last_error()));

    const uint32_t now_ms = HAL_GetTick();
    const int64_t current_count = sensor->total_count();
    if (std::llabs(current_count - last_move_count) >=
        kManualSpinStillThresholdCounts) {
      last_move_count = current_count;
      last_move_ms = now_ms;
    }

    if ((now_ms - spin_start_ms) >= kManualSpinMinDurationMs &&
        (now_ms - last_move_ms) >= kManualSpinStillDurationMs) {
      break;
    }
    HAL_Delay(5);
  }

  const int64_t total_delta = sensor->total_count() - start_count;
  const double degrees_travelled =
      static_cast<double>(total_delta) * kDegreesPerCount;
  const uint32_t spin_duration_ms = HAL_GetTick() - spin_start_ms;

  std::printf(
      "Manual spin complete: travelled=%.2f deg (delta=%ld counts, "
      "duration=%lu ms, final_raw=%u)\r\n",
      degrees_travelled, static_cast<long>(total_delta),
      static_cast<unsigned long>(spin_duration_ms),
      static_cast<unsigned>(sensor->raw_count()));
}
