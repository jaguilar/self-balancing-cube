#include <cstdio>

#include "cmsis_os2.h"
#include "main.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_rcc.h"
#include "stm32g4xx_ll_usart.h"

namespace {

void USART1_Init() {
  // 1. Enable GPIO Clocks (GPIOA for PA9, GPIOC for PC4/PC5)
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

  // Also configure PC4 as AF7 (USART1_TX) in case the board connects TX on PC4
  LL_GPIO_SetPinMode(GPIOC, LL_GPIO_PIN_4, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetAFPin_0_7(GPIOC, LL_GPIO_PIN_4, LL_GPIO_AF_7);
  LL_GPIO_SetPinSpeed(GPIOC, LL_GPIO_PIN_4, LL_GPIO_SPEED_FREQ_HIGH);
  LL_GPIO_SetPinOutputType(GPIOC, LL_GPIO_PIN_4, LL_GPIO_OUTPUT_PUSHPULL);
  LL_GPIO_SetPinPull(GPIOC, LL_GPIO_PIN_4, LL_GPIO_PULL_NO);

  // 4. Configure Baud Rate to 115200 based on APB2 clock
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
    while (!LL_USART_IsActiveFlag_TXE(USART1)) {
    }
    LL_USART_TransmitData8(USART1, static_cast<uint8_t>(*str++));
  }
  while (!LL_USART_IsActiveFlag_TC(USART1)) {
  }
}

}  // namespace

extern "C" {

void Setup() {
  USART1_Init();
  USART1_SendString("Hello, World!\r\n");
}

void Loop() {
  uint32_t count = 0;
  char buffer[64];

  while (1) {
    osDelay(1000);
    // Toggle LED (PC6) as visual heartbeat
    LL_GPIO_TogglePin(GPIOC, LL_GPIO_PIN_6);

    std::snprintf(buffer, sizeof(buffer), "Hello, World! (%lu)\r\n",
                  static_cast<unsigned long>(++count));
    USART1_SendString(buffer);
  }
}

}  // extern "C"
