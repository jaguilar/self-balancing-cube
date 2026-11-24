#pragma once

#include <cstdint>

#include "angle_sensor.h"
#include "battery_brake.h"
#include "can_driver.h"
#include "current_sense.h"
#include "motor_driver.h"
#include "stm32_hal.h"

namespace board {

enum class Target {
  kMotorDriverV1,
  kNucleoC552RE,
};

#ifdef BOARD_NUCLEO_C552RE
inline constexpr Target kTarget = Target::kNucleoC552RE;
#else
inline constexpr Target kTarget = Target::kMotorDriverV1;
#endif

struct UartPinConfig {
  hal_gpio_t port;
  uint32_t pin;
  hal_gpio_af_t alternate;
};

struct DebugUartConfig {
  hal_uart_t instance;
  UartPinConfig tx;
  UartPinConfig rx;
  uint32_t baud_rate;
};

/// Returns the debug/VCP UART configuration for the active board target.
static constexpr DebugUartConfig GetDebugUartConfig() {
  if constexpr (kTarget == Target::kNucleoC552RE) {
    return DebugUartConfig{
        .instance = HAL_UART2,
        .tx = {.port = HAL_GPIOA, .pin = HAL_GPIO_PIN_2, .alternate = HAL_GPIO_AF_7},
        .rx = {.port = HAL_GPIOA, .pin = HAL_GPIO_PIN_3, .alternate = HAL_GPIO_AF_7},
        .baud_rate = 115200,
    };
  } else {
    return DebugUartConfig{
        .instance = HAL_UART1,
        .tx = {.port = HAL_GPIOA, .pin = HAL_GPIO_PIN_6, .alternate = HAL_GPIO_AF_7},
        .rx = {.port = HAL_GPIOA, .pin = HAL_GPIO_PIN_10, .alternate = HAL_GPIO_AF_7},
        .baud_rate = 115200,
    };
  }
}

/// Returns the motor PWM driver configuration matching
/// src/driver/motor_driver_48-pinout.csv.
static constexpr stfoc::StTimerMotorConfig GetMotorDriverConfig(
    uint32_t pwm_freq = 25000U) {
  return stfoc::StTimerMotorConfig{
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
      .drv_coast =
          {
              .port = HAL_GPIOB,
              .pin = HAL_GPIO_PIN_8,
          },
      .drv_nfault =
          {
              .port = HAL_GPIOB,
              .pin = HAL_GPIO_PIN_7,
          },
      .pwm_freq = pwm_freq,
  };
}

/// Returns the AS5047P angle sensor configuration matching
/// docs/motor_selftest_wiring.md and src/driver/motor_driver_48-pinout.csv
/// (enc_i is omitted).
static constexpr stfoc::As5047pSensorConfig GetAngleSensorConfig() {
  return stfoc::As5047pSensorConfig{
      .spi_instance = HAL_SPI2,
      .sck =
          {
              .port = HAL_GPIOA,
              .pin = HAL_GPIO_PIN_5,
              .alternate = HAL_GPIO_AF_6,
          },
      .miso =
          {
              .port = HAL_GPIOA,
              .pin = HAL_GPIO_PIN_7,
              .alternate = HAL_GPIO_AF_6,
          },
      .mosi =
          {
              .port = HAL_GPIOB,
              .pin = HAL_GPIO_PIN_15,
              .alternate = HAL_GPIO_AF_5,
          },
      .nss =
          {
              .port = HAL_GPIOB,
              .pin = HAL_GPIO_PIN_1,
              .alternate = HAL_GPIO_AF_5,
          },
      // 144 MHz / 16 = 9 MHz (AS5047P max SCLK is 10 MHz).
      .spi_prescaler = HAL_SPI_BAUD_RATE_PRESCALER_16,
      .timer_instance = HAL_TIM5,
      .enc_a =
          {
              .port = HAL_GPIOA,
              .pin = HAL_GPIO_PIN_0,
              .alternate = HAL_GPIO_AF_2,
              .channel = HAL_TIM_CHANNEL_1,
              .capture_unit = HAL_TIM_IC_CAPTURE_UNIT_1,
              .input_source = HAL_TIM_INPUT_TIM5_TI1_GPIO,
          },
      .enc_b =
          {
              .port = HAL_GPIOB,
              .pin = HAL_GPIO_PIN_5,
              .alternate = HAL_GPIO_AF_2,
              .channel = HAL_TIM_CHANNEL_2,
              .capture_unit = HAL_TIM_IC_CAPTURE_UNIT_2,
              .input_source = HAL_TIM_INPUT_TIM5_TI2_GPIO,
          },
      .startup_delay_ms = 10U,
      .spi_timeout_ms = 10U,
  };
}

/// Returns the current sense configuration matching
/// docs/motor_selftest_wiring.md and src/driver/motor_driver_48-pinout.csv:
///   - drv_sense_a: PA1 -> ADC1_IN1
///   - drv_sense_b: PA2 -> ADC2_IN2
static constexpr stfoc::StAdcCurrentSenseConfig GetCurrentSenseConfig(
    float shunt_resistance = 0.005f, float amp_gain = 20.0f) {
  return stfoc::StAdcCurrentSenseConfig{
      .phase_a =
          {
              .adc_instance = HAL_ADC1,
              .channel = HAL_ADC_CHANNEL_1,
              .port = HAL_GPIOA,
              .pin = HAL_GPIO_PIN_1,
          },
      .phase_b =
          {
              .adc_instance = HAL_ADC2,
              .channel = HAL_ADC_CHANNEL_2,
              .port = HAL_GPIOA,
              .pin = HAL_GPIO_PIN_2,
          },
      .shunt_resistance = shunt_resistance,
      .amp_gain = amp_gain,
      .v_ref = 3.3f,
      .cal_a = 1.0f,
      .cal_b = 1.0f,
      .sampling_time = HAL_ADC_SAMPLING_TIME_13CYCLES,
  };
}

/// Returns the battery voltage sensing and brake comparator configuration
/// matching src/driver/motor_driver_48-pinout.csv and
/// hw/motor_driver/controller.kicad_sch:
///   - vm_sense: PB0 -> COMP1_INP2 + ADC2_IN6 (R23 = 8k25 / R24 = 1k1 0.1%,
///     8.5:1 ratio -> 0-28.05 V full scale at 3.3 V V_DDA)
///   - brake_en: PA3 -> COMP1_OUT (AF14), disabled on Nucleo-C552RE where PA3
///     is connected to the ST-Link VCP RX bridge
///   - COMP1_INM: internal DAC1_CH1 (disconnected from PA4 / enc_i)
static constexpr stfoc::StBatteryBrakeConfig GetBatteryBrakeConfig(
    float brake_threshold_v = 22.0f, float r_top_ohms = 8250.0f,
    float r_bottom_ohms = 1100.0f) {
  return stfoc::StBatteryBrakeConfig{
      .adc_instance = HAL_ADC2,
      .adc_channel = HAL_ADC_CHANNEL_6,
      .adc_sampling_time = HAL_ADC_SAMPLING_TIME_48CYCLES,
      .comp_instance = HAL_COMP1,
      .comp_input_plus = HAL_COMP_INPUT_PLUS_IO2,
      .comp_input_minus = HAL_COMP_INPUT_MINUS_DAC1_CH1,
      .comp_hysteresis = HAL_COMP_INPUT_HYSTERESIS_HIGH,
      .comp_power_mode = HAL_COMP_POWER_MODE_HIGH_SPEED,
      .sense_port = HAL_GPIOB,
      .sense_pin = HAL_GPIO_PIN_0,
      .enable_output_pin = (kTarget != Target::kNucleoC552RE),
      .output_port = HAL_GPIOA,
      .output_pin = HAL_GPIO_PIN_3,
      .output_af = HAL_GPIO_AF_14,
      .r_top_ohms = r_top_ohms,
      .r_bottom_ohms = r_bottom_ohms,
      .brake_threshold_v = brake_threshold_v,
      .v_ref = 3.3f,
      .calibrate_vdda = true,
  };
}

/// Returns the FDCAN1 configuration matching src/driver/motor_driver_48-pinout.csv:
///   - FDCAN1_TX: PA12 (AF9)
///   - FDCAN1_RX: PB12 (AF9)
static constexpr stfoc::StCanDriverConfig GetCanDriverConfig(
    hal_fdcan_mode_t mode = HAL_FDCAN_MODE_NORMAL) {
  return stfoc::StCanDriverConfig{
      .fdcan_instance = HAL_FDCAN1,
      .tx =
          {
              .port = HAL_GPIOA,
              .pin = HAL_GPIO_PIN_12,
              .alternate = HAL_GPIO_AF_9,
          },
      .rx =
          {
              .port = HAL_GPIOB,
              .pin = HAL_GPIO_PIN_12,
              .alternate = HAL_GPIO_AF_9,
          },
      .nominal_bit_timing =
          {
              .nominal_prescaler = 1U,
              .nominal_jump_width = 1U,
              .nominal_time_seg1 = 251U,
              .nominal_time_seg2 = 36U,
          },
      .frame_format = HAL_FDCAN_FRAME_FORMAT_FD_NO_BRS,
      .mode = mode,
      .enable_gpio = (mode != HAL_FDCAN_MODE_INTERNAL_LOOPBACK),
      .device_id_override = 0U,
  };
}

/// Initializes the debug UART peripheral and its GPIO pins for the active board.
/// Returns nullptr on failure.
hal_uart_handle_t* InitDebugUart();

}  // namespace board
