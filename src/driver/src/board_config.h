#pragma once

#include <cstdint>

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
constexpr DebugUartConfig debug_uart_config() {
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

/// Initializes the debug UART peripheral and its GPIO pins for the active board.
/// Returns nullptr on failure.
hal_uart_handle_t* InitDebugUart();

}  // namespace board
