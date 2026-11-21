#include "motor_driver.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

#include "CppUTest/TestHarness.h"
#include "stm32_hal.h"

namespace {

constexpr uint32_t kPwmFreqHz = 25000U;
constexpr uint32_t kTim8Prescaler = 7U;  // 144 MHz / 8 = 18 MHz tick rate.
constexpr uint32_t kCaptureTimeoutMs = 3U;

struct HarnessPwmChannel {
  const char* name;
  hal_gpio_t port;
  uint32_t pin;
  hal_gpio_af_t alternate;
  hal_tim_channel_t channel;
  hal_tim_ic_capture_unit_t capture_unit;
  hal_tim_channel_src_t input_source;
};

constexpr std::array<HarnessPwmChannel, 3> kHarnessPwmChannels = {{
    {
        .name = "drv_pwm_a",
        .port = HAL_GPIOC,
        .pin = HAL_GPIO_PIN_6,
        .alternate = HAL_GPIO_AF_3,
        .channel = HAL_TIM_CHANNEL_1,
        .capture_unit = HAL_TIM_IC_CAPTURE_UNIT_1,
        .input_source = HAL_TIM_INPUT_TIM8_TI1_GPIO,
    },
    {
        .name = "drv_pwm_b",
        .port = HAL_GPIOB,
        .pin = HAL_GPIO_PIN_13,
        .alternate = HAL_GPIO_AF_2,
        .channel = HAL_TIM_CHANNEL_2,
        .capture_unit = HAL_TIM_IC_CAPTURE_UNIT_2,
        .input_source = HAL_TIM_INPUT_TIM8_TI2_GPIO,
    },
    {
        .name = "drv_pwm_c",
        .port = HAL_GPIOC,
        .pin = HAL_GPIO_PIN_8,
        .alternate = HAL_GPIO_AF_3,
        .channel = HAL_TIM_CHANNEL_3,
        .capture_unit = HAL_TIM_IC_CAPTURE_UNIT_3,
        .input_source = HAL_TIM_INPUT_TIM8_TI3_GPIO,
    },
}};

constexpr hal_gpio_t kHarnessDrvEnPort = HAL_GPIOC;
constexpr uint32_t kHarnessDrvEnPin = HAL_GPIO_PIN_3;

bool IsCcFlagSet(hal_tim_channel_t channel) {
  switch (channel) {
    case HAL_TIM_CHANNEL_1:
      return LL_TIM_IsActiveFlag_CC1(TIM8) != 0U;
    case HAL_TIM_CHANNEL_2:
      return LL_TIM_IsActiveFlag_CC2(TIM8) != 0U;
    case HAL_TIM_CHANNEL_3:
      return LL_TIM_IsActiveFlag_CC3(TIM8) != 0U;
    default:
      return false;
  }
}

void ClearCcFlag(hal_tim_channel_t channel) {
  switch (channel) {
    case HAL_TIM_CHANNEL_1:
      LL_TIM_ClearFlag_CC1(TIM8);
      break;
    case HAL_TIM_CHANNEL_2:
      LL_TIM_ClearFlag_CC2(TIM8);
      break;
    case HAL_TIM_CHANNEL_3:
      LL_TIM_ClearFlag_CC3(TIM8);
      break;
    default:
      break;
  }
}

struct PwmMeasurement {
  bool has_edges = false;
  bool pin_high = false;
  float duty_cycle = 0.0f;
  float frequency_hz = 0.0f;
};

class Tim8Harness {
 public:
  void Init() {
    HAL_RCC_GPIOB_EnableClock();
    HAL_RCC_GPIOC_EnableClock();
    HAL_RCC_TIM8_EnableClock();

    hal_gpio_config_t drv_en_in_cfg{};
    drv_en_in_cfg.mode = HAL_GPIO_MODE_INPUT;
    drv_en_in_cfg.pull = HAL_GPIO_PULL_NO;
    CHECK_EQUAL(HAL_OK, HAL_GPIO_Init(kHarnessDrvEnPort, kHarnessDrvEnPin,
                                      &drv_en_in_cfg));

    for (const auto& ch : kHarnessPwmChannels) {
      hal_gpio_config_t ic_gpio_cfg{};
      ic_gpio_cfg.mode = HAL_GPIO_MODE_ALTERNATE;
      ic_gpio_cfg.pull = HAL_GPIO_PULL_DOWN;
      ic_gpio_cfg.speed = HAL_GPIO_SPEED_FREQ_HIGH;
      ic_gpio_cfg.alternate = ch.alternate;
      CHECK_EQUAL(HAL_OK, HAL_GPIO_Init(ch.port, ch.pin, &ic_gpio_cfg));
    }

    CHECK_EQUAL(HAL_OK, HAL_TIM_Init(&htim8_, HAL_TIM8));

    hal_tim_config_t tim_cfg{};
    tim_cfg.prescaler = kTim8Prescaler;
    tim_cfg.counter_mode = HAL_TIM_COUNTER_UP;
    tim_cfg.period = 0xFFFFU;
    tim_cfg.repetition_counter = 0U;
    tim_cfg.clock_sel.clock_source = HAL_TIM_CLK_INTERNAL;
    CHECK_EQUAL(HAL_OK, HAL_TIM_SetConfig(&htim8_, &tim_cfg));

    for (const auto& ch : kHarnessPwmChannels) {
      hal_tim_ic_channel_config_t ic_ch_cfg{};
      ic_ch_cfg.source = ch.input_source;
      ic_ch_cfg.polarity = HAL_TIM_IC_RISING_FALLING;
      ic_ch_cfg.filter = HAL_TIM_FDIV1;
      CHECK_EQUAL(HAL_OK,
                  HAL_TIM_IC_SetConfigChannel(&htim8_, ch.channel, &ic_ch_cfg));

      hal_tim_ic_capture_unit_config_t ic_unit_cfg{};
      ic_unit_cfg.source = HAL_TIM_IC_DIRECT;
      ic_unit_cfg.prescaler = HAL_TIM_IC_DIV1;
      CHECK_EQUAL(HAL_OK, HAL_TIM_IC_SetConfigCaptureUnit(
                              &htim8_, ch.capture_unit, &ic_unit_cfg));
    }

    CHECK_EQUAL(HAL_OK, HAL_TIM_Start(&htim8_));
    tick_hz_ = HAL_RCC_GetPCLK2Freq() / (kTim8Prescaler + 1U);
    initialized_ = true;
  }

  void Deinit() {
    if (!initialized_) {
      return;
    }
    (void)HAL_TIM_Stop(&htim8_);
    (void)HAL_TIM_DeInit(&htim8_);
    for (const auto& ch : kHarnessPwmChannels) {
      HAL_GPIO_DeInit(ch.port, ch.pin);
    }
    HAL_GPIO_DeInit(kHarnessDrvEnPort, kHarnessDrvEnPin);
    initialized_ = false;
  }

  bool ReadDrvEn() const {
    return HAL_GPIO_ReadPin(kHarnessDrvEnPort, kHarnessDrvEnPin) ==
           HAL_GPIO_PIN_SET;
  }

  PwmMeasurement Measure(std::size_t idx) {
    const auto& ch = kHarnessPwmChannels[idx];
    PwmMeasurement result{};

    struct Edge {
      uint16_t timestamp;
      bool rising;
    };
    std::array<Edge, 6> edges{};
    std::size_t count = 0;

    ClearCcFlag(ch.channel);
    CHECK_EQUAL(HAL_OK, HAL_TIM_IC_StartChannel(&htim8_, ch.channel));

    const uint32_t start_ms = HAL_GetTick();
    while (count < edges.size() &&
           (HAL_GetTick() - start_ms) < kCaptureTimeoutMs) {
      if (IsCcFlagSet(ch.channel)) {
        const uint32_t cap =
            HAL_TIM_IC_ReadChannelCapturedValue(&htim8_, ch.channel);
        const bool is_high =
            (HAL_GPIO_ReadPin(ch.port, ch.pin) == HAL_GPIO_PIN_SET);
        ClearCcFlag(ch.channel);
        edges[count++] = {static_cast<uint16_t>(cap & 0xFFFFU), is_high};
      }
    }

    CHECK_EQUAL(HAL_OK, HAL_TIM_IC_StopChannel(&htim8_, ch.channel));

    result.pin_high = (HAL_GPIO_ReadPin(ch.port, ch.pin) == HAL_GPIO_PIN_SET);
    if (count < 4U) {
      result.has_edges = false;
      return result;
    }

    // Find a rising -> falling -> rising sequence.
    for (std::size_t i = 0; i + 2 < count; ++i) {
      if (edges[i].rising && !edges[i + 1].rising && edges[i + 2].rising) {
        const uint16_t high_ticks =
            static_cast<uint16_t>(edges[i + 1].timestamp - edges[i].timestamp);
        const uint16_t period_ticks =
            static_cast<uint16_t>(edges[i + 2].timestamp - edges[i].timestamp);
        if (period_ticks > 0U) {
          result.has_edges = true;
          result.duty_cycle = static_cast<float>(high_ticks) /
                              static_cast<float>(period_ticks);
          result.frequency_hz =
              static_cast<float>(tick_hz_) / static_cast<float>(period_ticks);
          return result;
        }
      }
    }

    result.has_edges = false;
    return result;
  }

 private:
  hal_tim_handle_t htim8_{};
  uint32_t tick_hz_{0};
  bool initialized_{false};
};

}  // namespace

TEST_GROUP(MotorDriver) {
  Tim8Harness harness;
  std::optional<stfoc::StTimerMotorDriver> driver;

  void setup() override {
    harness.Init();
    driver.emplace(stfoc::DefaultMotorDriverConfig(kPwmFreqHz));
  }

  void teardown() override {
    driver.reset();
    harness.Deinit();
  }
};

TEST(MotorDriver, InitEnableDisable) {
  driver->voltage_power_supply = 12.0f;
  driver->voltage_limit = 12.0f;

  LONGS_EQUAL(1, driver->init());
  CHECK_TRUE(driver->initialized);
  CHECK_FALSE(harness.ReadDrvEn());

  for (std::size_t i = 0; i < 3; ++i) {
    const auto m = harness.Measure(i);
    CHECK_FALSE(m.has_edges);
    CHECK_FALSE(m.pin_high);
  }

  driver->enable();
  CHECK_TRUE(harness.ReadDrvEn());
  HAL_Delay(2);

  for (std::size_t i = 0; i < 3; ++i) {
    const auto m = harness.Measure(i);
    CHECK_TRUE(m.has_edges);
    DOUBLES_EQUAL(0.50, m.duty_cycle, 0.02);
    DOUBLES_EQUAL(static_cast<double>(kPwmFreqHz), m.frequency_hz, 500.0);
  }

  driver->disable();
  CHECK_FALSE(harness.ReadDrvEn());

  for (std::size_t i = 0; i < 3; ++i) {
    const auto m = harness.Measure(i);
    CHECK_FALSE(m.has_edges);
    CHECK_FALSE(m.pin_high);
  }
}

TEST(MotorDriver, SetPwmVoltagesAndClamping) {
  driver->voltage_power_supply = 12.0f;
  driver->voltage_limit = 9.0f;

  LONGS_EQUAL(1, driver->init());
  driver->enable();

  driver->setPwm(3.0f, 6.0f, 9.0f);
  HAL_Delay(1);

  const auto m_a = harness.Measure(0);
  const auto m_b = harness.Measure(1);
  const auto m_c = harness.Measure(2);
  CHECK_TRUE(m_a.has_edges);
  CHECK_TRUE(m_b.has_edges);
  CHECK_TRUE(m_c.has_edges);
  DOUBLES_EQUAL(0.25, m_a.duty_cycle, 0.02);
  DOUBLES_EQUAL(0.50, m_b.duty_cycle, 0.02);
  DOUBLES_EQUAL(0.75, m_c.duty_cycle, 0.02);

  // Test clamping: 12V -> 9V limit (75%), -1V -> 0V (0%), 1.5V -> 12.5%.
  driver->setPwm(12.0f, -1.0f, 1.5f);
  HAL_Delay(1);

  const auto m_a2 = harness.Measure(0);
  const auto m_b2 = harness.Measure(1);
  const auto m_c2 = harness.Measure(2);
  CHECK_TRUE(m_a2.has_edges);
  DOUBLES_EQUAL(0.75, m_a2.duty_cycle, 0.02);
  CHECK_FALSE(m_b2.has_edges);
  CHECK_FALSE(m_b2.pin_high);
  CHECK_TRUE(m_c2.has_edges);
  DOUBLES_EQUAL(0.125, m_c2.duty_cycle, 0.02);
}

TEST(MotorDriver, SetPhaseState) {
  driver->voltage_power_supply = 12.0f;
  driver->voltage_limit = 12.0f;

  LONGS_EQUAL(1, driver->init());
  driver->enable();
  driver->setPwm(6.0f, 6.0f, 6.0f);
  HAL_Delay(1);

  driver->setPhaseState(PhaseState::PHASE_OFF, PhaseState::PHASE_ON,
                        PhaseState::PHASE_ON);
  HAL_Delay(1);
  CHECK_FALSE(harness.Measure(0).has_edges);
  CHECK_FALSE(harness.Measure(0).pin_high);
  CHECK_TRUE(harness.Measure(1).has_edges);
  DOUBLES_EQUAL(0.50, harness.Measure(1).duty_cycle, 0.02);
  CHECK_TRUE(harness.Measure(2).has_edges);
  DOUBLES_EQUAL(0.50, harness.Measure(2).duty_cycle, 0.02);

  driver->setPhaseState(PhaseState::PHASE_ON, PhaseState::PHASE_OFF,
                        PhaseState::PHASE_OFF);
  HAL_Delay(1);
  CHECK_TRUE(harness.Measure(0).has_edges);
  DOUBLES_EQUAL(0.50, harness.Measure(0).duty_cycle, 0.02);
  CHECK_FALSE(harness.Measure(1).has_edges);
  CHECK_FALSE(harness.Measure(1).pin_high);
  CHECK_FALSE(harness.Measure(2).has_edges);
  CHECK_FALSE(harness.Measure(2).pin_high);

  driver->setPhaseState(PhaseState::PHASE_ON, PhaseState::PHASE_ON,
                        PhaseState::PHASE_ON);
  HAL_Delay(1);
  for (std::size_t i = 0; i < 3; ++i) {
    const auto m = harness.Measure(i);
    CHECK_TRUE(m.has_edges);
    DOUBLES_EQUAL(0.50, m.duty_cycle, 0.02);
  }
}
