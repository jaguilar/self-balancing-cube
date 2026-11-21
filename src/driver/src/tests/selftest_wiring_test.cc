#include <array>
#include <cstdint>
#include <cstdio>

#include "CppUTest/TestHarness.h"
#include "stm32_hal.h"

namespace {

struct GpioPin {
  hal_gpio_t port;
  uint32_t pin;
  const char* name;
};

struct WiringPair {
  const char* signal;
  GpioPin dut;
  GpioPin harness;
};

constexpr uint32_t kSettleDelayMs = 20U;

// 13 of the 15 jumper pairs from docs/motor_selftest_wiring.md.
// Intentionally omitted:
//   - brake_en (PA3 <-> PA15)
//   - drv_sense_b (PA2 <-> PC4)
// PA2 (TX) and PA3 (RX) are used by Nucleo-C562RE ST-Link VCP USART2,
// so PA2, PA3, PC4, and PA15 are left untouched.
constexpr std::array<WiringPair, 13> kWiringPairs{{
    {.signal = "drv_pwm_a",
     .dut = {HAL_GPIOA, HAL_GPIO_PIN_8, "PA8"},
     .harness = {HAL_GPIOC, HAL_GPIO_PIN_6, "PC6"}},
    {.signal = "drv_pwm_b",
     .dut = {HAL_GPIOA, HAL_GPIO_PIN_9, "PA9"},
     .harness = {HAL_GPIOB, HAL_GPIO_PIN_13, "PB13"}},
    {.signal = "drv_pwm_c",
     .dut = {HAL_GPIOA, HAL_GPIO_PIN_11, "PA11"},
     .harness = {HAL_GPIOC, HAL_GPIO_PIN_8, "PC8"}},
    {.signal = "enc_a",
     .dut = {HAL_GPIOA, HAL_GPIO_PIN_0, "PA0"},
     .harness = {HAL_GPIOC, HAL_GPIO_PIN_0, "PC0"}},
    {.signal = "enc_b",
     .dut = {HAL_GPIOB, HAL_GPIO_PIN_5, "PB5"},
     .harness = {HAL_GPIOC, HAL_GPIO_PIN_1, "PC1"}},
    {.signal = "spi2_sck",
     .dut = {HAL_GPIOA, HAL_GPIO_PIN_5, "PA5"},
     .harness = {HAL_GPIOC, HAL_GPIO_PIN_10, "PC10"}},
    {.signal = "spi2_miso",
     .dut = {HAL_GPIOA, HAL_GPIO_PIN_7, "PA7"},
     .harness = {HAL_GPIOC, HAL_GPIO_PIN_11, "PC11"}},
    {.signal = "spi2_mosi",
     .dut = {HAL_GPIOB, HAL_GPIO_PIN_15, "PB15"},
     .harness = {HAL_GPIOB, HAL_GPIO_PIN_2, "PB2"}},
    {.signal = "vm_sense",
     .dut = {HAL_GPIOB, HAL_GPIO_PIN_0, "PB0"},
     .harness = {HAL_GPIOB, HAL_GPIO_PIN_14, "PB14"}},
    {.signal = "drv_sense_a",
     .dut = {HAL_GPIOA, HAL_GPIO_PIN_1, "PA1"},
     .harness = {HAL_GPIOC, HAL_GPIO_PIN_12, "PC12"}},
    {.signal = "drv_en",
     .dut = {HAL_GPIOB, HAL_GPIO_PIN_6, "PB6"},
     .harness = {HAL_GPIOC, HAL_GPIO_PIN_3, "PC3"}},
    {.signal = "drv_nfault",
     .dut = {HAL_GPIOB, HAL_GPIO_PIN_7, "PB7"},
     .harness = {HAL_GPIOC, HAL_GPIO_PIN_5, "PC5"}},
}};

void ConfigureInput(const GpioPin& p) {
  hal_gpio_config_t cfg{};
  cfg.mode = HAL_GPIO_MODE_INPUT;
  // Disable internal pull resistors so ~40 kOhm pulls do not form a divider
  // with the 10 kOhm RC-filter resistors on vm_sense and drv_sense_a.
  cfg.pull = HAL_GPIO_PULL_NO;
  HAL_GPIO_Init(p.port, p.pin, &cfg);
}

void ConfigureOutput(const GpioPin& p, hal_gpio_pin_state_t init_state) {
  hal_gpio_config_t cfg{};
  cfg.mode = HAL_GPIO_MODE_OUTPUT;
  cfg.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
  cfg.pull = HAL_GPIO_PULL_NO;
  cfg.speed = HAL_GPIO_SPEED_FREQ_LOW;
  cfg.init_state = init_state;
  HAL_GPIO_Init(p.port, p.pin, &cfg);
}

void TestDirection(const GpioPin& driver, const GpioPin& receiver,
                   bool& high_ok, bool& low_ok) {
  ConfigureInput(receiver);
  ConfigureOutput(driver, HAL_GPIO_PIN_SET);
  HAL_Delay(kSettleDelayMs);
  high_ok = (HAL_GPIO_ReadPin(receiver.port, receiver.pin) == HAL_GPIO_PIN_SET);

  HAL_GPIO_WritePin(driver.port, driver.pin, HAL_GPIO_PIN_RESET);
  HAL_Delay(kSettleDelayMs);
  low_ok = (HAL_GPIO_ReadPin(receiver.port, receiver.pin) == HAL_GPIO_PIN_RESET);

  ConfigureInput(driver);
}

}  // namespace

TEST_GROUP(SelftestWiring){};

TEST(SelftestWiring, AllPairsBidirectional) {
  HAL_RCC_GPIOA_EnableClock();
  HAL_RCC_GPIOB_EnableClock();
  HAL_RCC_GPIOC_EnableClock();

  for (const auto& pair : kWiringPairs) {
    ConfigureInput(pair.dut);
    ConfigureInput(pair.harness);
  }

  int failed_pairs = 0;
  for (const auto& pair : kWiringPairs) {
    bool dut_to_harness_high = false;
    bool dut_to_harness_low = false;
    bool harness_to_dut_high = false;
    bool harness_to_dut_low = false;

    TestDirection(pair.dut, pair.harness, dut_to_harness_high,
                  dut_to_harness_low);
    TestDirection(pair.harness, pair.dut, harness_to_dut_high,
                  harness_to_dut_low);

    const bool pass = dut_to_harness_high && dut_to_harness_low &&
                      harness_to_dut_high && harness_to_dut_low;
    if (!pass) {
      ++failed_pairs;
    }

    std::printf(
        "%-12s %4s <-> %-4s : %s (dut->harness H:%d L:%d, harness->dut H:%d L:%d)\r\n",
        pair.signal, pair.dut.name, pair.harness.name,
        pass ? "PASS" : "FAIL", dut_to_harness_high ? 1 : 0,
        dut_to_harness_low ? 1 : 0, harness_to_dut_high ? 1 : 0,
        harness_to_dut_low ? 1 : 0);
  }

  LONGS_EQUAL(0, failed_pairs);
}
