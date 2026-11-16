#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "cmsis_os2.h"
#include "i2c.h"
#include "main.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_rcc.h"
#include "stm32g4xx_ll_usart.h"

extern I2C_HandleTypeDef hi2c1;

namespace {

#ifndef M_PI
constexpr float kPi = 3.14159265358979323846f;
#else
constexpr float kPi = static_cast<float>(M_PI);
#endif
constexpr float kRadToDeg = 180.0f / kPi;

// ICM-42670-P Registers (User Bank 0)
constexpr uint8_t REG_SIGNAL_PATH_RESET = 0x02;
constexpr uint8_t REG_ACCEL_DATA_X1 = 0x0B;  // 0x0B to 0x10: X1, X0, Y1, Y0, Z1, Z0
constexpr uint8_t REG_PWR_MGMT0 = 0x1F;
constexpr uint8_t REG_ACCEL_CONFIG0 = 0x21;
constexpr uint8_t REG_WHO_AM_I = 0x75;

// Expected WHO_AM_I value
constexpr uint8_t ICM42670P_WHO_AM_I_VAL = 0x67;

// Candidate 7-bit I2C addresses
constexpr uint8_t ICM42670P_ADDR_AD0_LOW = 0x68;
constexpr uint8_t ICM42670P_ADDR_AD0_HIGH = 0x69;

// Sensitivity for ±2g mode: 16,384 LSB/g
constexpr float ACCEL_SCALE_2G = 16384.0f;

uint16_t g_detected_i2c_addr = 0;  // 8-bit shifted address for HAL

void USART1_Init() {
  // 1. Enable GPIO Clocks (GPIOA for PA9, GPIOC for PC4)
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOC);

  // 2. Enable USART1 Peripheral Clock
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);

  // 3. Configure PA9 as AF7 (USART1_TX)
  LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_9, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetAFPin_8_15(GPIOA, LL_GPIO_PIN_9, LL_GPIO_AF_7);
  LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_9, LL_GPIO_SPEED_FREQ_HIGH);
  LL_GPIO_SetPinOutputType(GPIOA, LL_GPIO_PIN_9, LL_GPIO_OUTPUT_PUSHPULL);
  LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_9, LL_GPIO_PULL_NO);

  // Also configure PC4 as AF7 (USART1_TX) in case TX is routed to PC4
  LL_GPIO_SetPinMode(GPIOC, LL_GPIO_PIN_4, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetAFPin_0_7(GPIOC, LL_GPIO_PIN_4, LL_GPIO_AF_7);
  LL_GPIO_SetPinSpeed(GPIOC, LL_GPIO_PIN_4, LL_GPIO_SPEED_FREQ_HIGH);
  LL_GPIO_SetPinOutputType(GPIOC, LL_GPIO_PIN_4, LL_GPIO_OUTPUT_PUSHPULL);
  LL_GPIO_SetPinPull(GPIOC, LL_GPIO_PIN_4, LL_GPIO_PULL_NO);

  // 4. Configure Baud Rate to 115200 based on system clock
  uint32_t freq = 16000000;
  if (LL_RCC_GetSysClkSource() == LL_RCC_SYS_CLKSOURCE_STATUS_PLL) {
    freq = 160000000;
  }
  LL_USART_SetBaudRate(USART1, freq, LL_USART_PRESCALER_DIV1,
                       LL_USART_OVERSAMPLING_16, 115200);

  // 5. 8 data bits, 1 stop bit, no parity, enable TX
  LL_USART_SetDataWidth(USART1, LL_USART_DATAWIDTH_8B);
  LL_USART_SetStopBitsLength(USART1, LL_USART_STOPBITS_1);
  LL_USART_SetParity(USART1, LL_USART_PARITY_NONE);
  LL_USART_EnableDirectionTx(USART1);
  LL_USART_Enable(USART1);
}

void USART1_SendString(const char* str) {
  while (*str) {
    while (!LL_USART_IsActiveFlag_TXE(USART1)) {}
    LL_USART_TransmitData8(USART1, static_cast<uint8_t>(*str++));
  }
  while (!LL_USART_IsActiveFlag_TC(USART1)) {}
}

void LogPrint(const char* format, ...) {
  char buf[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  USART1_SendString(buf);
}

bool Icm42670p_Init() {
  LogPrint("Scanning for ICM-42670-P on I2C1...\r\n");

  const uint8_t candidate_addrs[] = {
      ICM42670P_ADDR_AD0_LOW,
      ICM42670P_ADDR_AD0_HIGH,
  };

  g_detected_i2c_addr = 0;
  for (uint8_t addr : candidate_addrs) {
    uint8_t who = 0;
    uint16_t hal_addr = static_cast<uint16_t>(addr << 1);
    if (HAL_I2C_Mem_Read(&hi2c1, hal_addr, REG_WHO_AM_I, I2C_MEMADD_SIZE_8BIT,
                         &who, 1, 100) == HAL_OK) {
      LogPrint("  Address 0x%02X responded with WHO_AM_I = 0x%02X\r\n", addr, who);
      if (who == ICM42670P_WHO_AM_I_VAL) {
        g_detected_i2c_addr = hal_addr;
        LogPrint("ICM-42670-P found at 7-bit addr 0x%02X!\r\n", addr);
        break;
      }
    }
  }

  if (g_detected_i2c_addr == 0) {
    LogPrint("ERROR: ICM-42670-P not found on I2C1 (checked 0x68 and 0x69)\r\n");
    return false;
  }

  // 1. Soft-reset device configuration (bit 4: SOFT_RESET_DEVICE_CONFIG)
  uint8_t reset_val = 0x10;
  HAL_I2C_Mem_Write(&hi2c1, g_detected_i2c_addr, REG_SIGNAL_PATH_RESET,
                    I2C_MEMADD_SIZE_8BIT, &reset_val, 1, 100);
  osDelay(10);  // Allow reset to complete

  // 2. Configure Accelerometer:
  // ACCEL_UI_FS_SEL (bits 6:5) = 11 (±2g, 16384 LSB/g)
  // ACCEL_ODR (bits 3:0) = 0111 (100 Hz)
  uint8_t accel_cfg = (3 << 5) | (7);
  if (HAL_I2C_Mem_Write(&hi2c1, g_detected_i2c_addr, REG_ACCEL_CONFIG0,
                        I2C_MEMADD_SIZE_8BIT, &accel_cfg, 1, 100) != HAL_OK) {
    LogPrint("ERROR: Failed to configure ACCEL_CONFIG0\r\n");
    return false;
  }

  // 3. Power on Accelerometer in Low-Noise (LN) Mode:
  uint8_t pwr_cfg = 0x0F;
  if (HAL_I2C_Mem_Write(&hi2c1, g_detected_i2c_addr, REG_PWR_MGMT0,
                        I2C_MEMADD_SIZE_8BIT, &pwr_cfg, 1, 100) != HAL_OK) {
    LogPrint("ERROR: Failed to write PWR_MGMT0\r\n");
    return false;
  }
  osDelay(50);

  LogPrint("ICM-42670-P initialized successfully (±2g range, LN mode).\r\n");
  return true;
}

bool Icm42670p_ReadAccel(float& ax, float& ay, float& az) {
  if (g_detected_i2c_addr == 0) return false;

  uint8_t raw_data[6];
  if (HAL_I2C_Mem_Read(&hi2c1, g_detected_i2c_addr, REG_ACCEL_DATA_X1,
                       I2C_MEMADD_SIZE_8BIT, raw_data, 6, 50) != HAL_OK) {
    return false;
  }

  int16_t raw_x = static_cast<int16_t>((raw_data[0] << 8) | raw_data[1]);
  int16_t raw_y = static_cast<int16_t>((raw_data[2] << 8) | raw_data[3]);
  int16_t raw_z = static_cast<int16_t>((raw_data[4] << 8) | raw_data[5]);

  ax = static_cast<float>(raw_x) / ACCEL_SCALE_2G;
  ay = static_cast<float>(raw_y) / ACCEL_SCALE_2G;
  az = static_cast<float>(raw_z) / ACCEL_SCALE_2G;
  return true;
}

}  // namespace

extern "C" {

void Setup() {
  USART1_Init();
  LogPrint("\r\n========================================\r\n");
  LogPrint("  ICM-42670-P Attitude Monitor (250ms)  \r\n");
  LogPrint("========================================\r\n");

  while (!Icm42670p_Init()) {
    LogPrint("Retrying IMU initialization in 1s...\r\n");
    osDelay(1000);
  }

  LogPrint("\r\nStreaming X and Y attitude angles every 250ms:\r\n");
}

void Loop() {
  uint32_t sample_count = 0;
  float ax = 0.0f, ay = 0.0f, az = 0.0f;

  while (1) {
    osDelay(250);

    // Toggle onboard heartbeat LED (PC6)
    LL_GPIO_TogglePin(GPIOC, LL_GPIO_PIN_6);

    if (Icm42670p_ReadAccel(ax, ay, az)) {
      // Attitude (inclination) of X and Y axes relative to horizontal plane
      float att_x = std::atan2(ax, std::sqrt(ay * ay + az * az)) * kRadToDeg;
      float att_y = std::atan2(ay, std::sqrt(ax * ax + az * az)) * kRadToDeg;

      // Standard Roll (rotation around X) and Pitch (rotation around Y)
      float roll = std::atan2(ay, az) * kRadToDeg;
      float pitch = std::atan2(-ax, std::sqrt(ay * ay + az * az)) * kRadToDeg;

      LogPrint("[#%5lu] Att(X): %+6.2f deg | Att(Y): %+6.2f deg | Roll: %+6.2f deg | Pitch: %+6.2f deg | Accel(g): [%+5.2f, %+5.2f, %+5.2f]\r\n",
               static_cast<unsigned long>(++sample_count),
               static_cast<double>(att_x),
               static_cast<double>(att_y),
               static_cast<double>(roll),
               static_cast<double>(pitch),
               static_cast<double>(ax),
               static_cast<double>(ay),
               static_cast<double>(az));
    } else {
      LogPrint("[#%5lu] I2C read error from ICM-42670-P\r\n",
               static_cast<unsigned long>(++sample_count));
    }
  }
}

}  // extern "C"
