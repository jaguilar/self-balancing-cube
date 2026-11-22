#include "Arduino.h"
#include "stm32_hal.h"

void delay(uint32_t ms) { HAL_Delay(ms); }

void delayMicroseconds(uint32_t us) {
  const uint32_t start = DWT->CYCCNT;
  const uint32_t ticks = us * (SystemCoreClock / 1'000'000U);
  while ((DWT->CYCCNT - start) < ticks) {
  }
}

uint32_t millis() { return HAL_GetTick(); }

uint32_t micros() { return DWT->CYCCNT / (SystemCoreClock / 1'000'000U); }
