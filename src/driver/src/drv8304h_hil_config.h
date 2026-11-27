#pragma once

#include "as5048a_angle_sensor.h"
#include "board_config.h"

namespace board::drv8304h_hil {

/// Returns the AS5048A SPI angle sensor configuration matching
/// docs/nucleo_c562re_motor_wiring.md:
///   - SPI2_SCK:  PA5 (AF6, CN10-11)
///   - SPI2_MISO: PA7 (AF6, CN10-15)
///   - SPI2_MOSI: PB15 (AF5, CN10-26)
///   - enc_nss:   PB1 (AF5, CN10-18)
static constexpr stfoc::As5048aSensorConfig GetAs5048aSensorConfig() {
  return stfoc::As5048aSensorConfig{
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
      // 144 MHz / 16 = 9 MHz (AS5048A max SCLK is 10 MHz).
      .spi_prescaler = HAL_SPI_BAUD_RATE_PRESCALER_16,
      .trigger_timer = board::GetMotorDriverConfig().timer_instance,
      .tx_dma_channel = board::kAngleSensorTxDmaChannel,
      .rx_dma_channel = board::kAngleSensorRxDmaChannel,
      .startup_delay_ms = 10U,
      .spi_timeout_ms = 10U,
  };
}

/// Returns the current sense configuration for the DRV8304H HIL test wiring
/// (docs/nucleo_c562re_motor_wiring.md):
///   - drv_sense_a: PA1 -> ADC1_IN1 (CN7-30)
///   - drv_sense_b: PC5 -> ADC2_IN5 (CN7-36, remapped from PA2)
///   - drv_cal:     PC7 -> GPIO out (CN10-4, hardware CAL pin)
static constexpr stfoc::StAdcCurrentSenseConfig GetCurrentSenseConfig(
    float shunt_resistance = 0.005f, float amp_gain = 20.0f) {
  auto cfg = board::GetCurrentSenseConfig(shunt_resistance, amp_gain);
  cfg.phase_b = stfoc::AdcChannelConfig{
      .adc_instance = HAL_ADC2,
      .channel = HAL_ADC_CHANNEL_5,
      .port = HAL_GPIOC,
      .pin = HAL_GPIO_PIN_5,
  };
  cfg.set_cal_mode = &board::SetHardwareCalMode;
  cfg.cal_ctx = nullptr;
  return cfg;
}

}  // namespace board::drv8304h_hil
