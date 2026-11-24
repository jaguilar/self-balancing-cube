#include "board_config.h"

namespace board {

namespace {

hal_uart_handle_t debug_uart_handle;

void EnableGpioClock(hal_gpio_t port) {
  switch (port) {
    case HAL_GPIOA:
      HAL_RCC_GPIOA_EnableClock();
      break;
    case HAL_GPIOB:
      HAL_RCC_GPIOB_EnableClock();
      break;
    case HAL_GPIOC:
      HAL_RCC_GPIOC_EnableClock();
      break;
    case HAL_GPIOD:
      HAL_RCC_GPIOD_EnableClock();
      break;
    case HAL_GPIOE:
      HAL_RCC_GPIOE_EnableClock();
      break;
    case HAL_GPIOH:
      HAL_RCC_GPIOH_EnableClock();
      break;
    default:
      break;
  }
}

}  // namespace

hal_uart_handle_t* InitDebugUart() {
  constexpr DebugUartConfig cfg = GetDebugUartConfig();

  if (HAL_UART_Init(&debug_uart_handle, cfg.instance) != HAL_OK) {
    return nullptr;
  }

  if constexpr (kTarget == Target::kNucleoC552RE) {
    HAL_RCC_USART2_EnableClock();
    if (HAL_RCC_USART2_SetKernelClkSource(HAL_RCC_USART2_CLK_SRC_PCLK1) != HAL_OK) {
      return nullptr;
    }
  } else {
    HAL_RCC_USART1_EnableClock();
    if (HAL_RCC_USART1_SetKernelClkSource(HAL_RCC_USART1_CLK_SRC_PCLK2) != HAL_OK) {
      return nullptr;
    }
  }

  hal_uart_config_t uart_config{};
  uart_config.baud_rate = cfg.baud_rate;
  uart_config.clock_prescaler = HAL_UART_PRESCALER_DIV1;
  uart_config.word_length = HAL_UART_WORD_LENGTH_8_BIT;
  uart_config.stop_bits = HAL_UART_STOP_BIT_1;
  uart_config.parity = HAL_UART_PARITY_NONE;
  uart_config.direction = HAL_UART_DIRECTION_TX_RX;
  uart_config.hw_flow_ctl = HAL_UART_HW_CONTROL_NONE;
  uart_config.oversampling = HAL_UART_OVERSAMPLING_16;
  uart_config.one_bit_sampling = HAL_UART_ONE_BIT_SAMPLE_DISABLE;

  if (HAL_UART_SetConfig(&debug_uart_handle, &uart_config) != HAL_OK) {
    return nullptr;
  }

  EnableGpioClock(cfg.tx.port);
  EnableGpioClock(cfg.rx.port);

  hal_gpio_config_t gpio_config{};
  gpio_config.mode = HAL_GPIO_MODE_ALTERNATE;
  gpio_config.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
  gpio_config.speed = HAL_GPIO_SPEED_FREQ_LOW;

  gpio_config.pull = HAL_GPIO_PULL_UP;
  gpio_config.alternate = cfg.tx.alternate;
  HAL_GPIO_Init(cfg.tx.port, cfg.tx.pin, &gpio_config);

  gpio_config.pull = HAL_GPIO_PULL_NO;
  gpio_config.alternate = cfg.rx.alternate;
  HAL_GPIO_Init(cfg.rx.port, cfg.rx.pin, &gpio_config);

  return &debug_uart_handle;
}

}  // namespace board
