#include "Arduino.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "stm32_hal.h"

namespace {
PrintfPrint g_serial_instance;
}  // namespace

PrintfPrint* SerialPtr = &g_serial_instance;

void delay(uint32_t ms) { HAL_Delay(ms); }

void delayMicroseconds(uint32_t us) {
  const uint32_t start = DWT->CYCCNT;
  const uint32_t ticks = us * (SystemCoreClock / 1'000'000U);
  while ((DWT->CYCCNT - start) < ticks) {
  }
}

uint32_t millis() { return HAL_GetTick(); }

uint32_t micros() { return DWT->CYCCNT / (SystemCoreClock / 1'000'000U); }

std::size_t PrintfPrint::write(const uint8_t* buffer, std::size_t size) {
  const std::size_t written = std::fwrite(buffer, 1, size, stdout);
  std::fflush(stdout);
  return written;
}

std::size_t Print::print(const char* s) {
  return write(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
}

std::size_t Print::print(char c) {
  return write(reinterpret_cast<const uint8_t*>(&c), 1);
}

std::size_t Print::print(unsigned char b, int base) {
  return print(static_cast<unsigned long>(b), base);
}

std::size_t Print::print(int n, int base) {
  return print(static_cast<long>(n), base);
}

std::size_t Print::print(unsigned int n, int base) {
  return print(static_cast<unsigned long>(n), base);
}

std::size_t Print::print(long n, int base) {
  char buf[32];
  int len = 0;
  if (base == 10) {
    len = std::snprintf(buf, sizeof(buf), "%ld", n);
  } else {
    len = std::snprintf(buf, sizeof(buf),
                        base == 16 ? "%lx" : (base == 8 ? "%lo" : "%lu"),
                        static_cast<unsigned long>(n));
  }
  return (len > 0) ? write(reinterpret_cast<const uint8_t*>(buf),
                           static_cast<std::size_t>(len))
                   : 0U;
}

std::size_t Print::print(unsigned long n, int base) {
  char buf[32];
  const int len = std::snprintf(
      buf, sizeof(buf), base == 16 ? "%lx" : (base == 8 ? "%lo" : "%lu"), n);
  return (len > 0) ? write(reinterpret_cast<const uint8_t*>(buf),
                           static_cast<std::size_t>(len))
                   : 0U;
}

std::size_t Print::print(double n, int digits) {
  if (std::isnan(n)) return print("nan");
  if (std::isinf(n)) return print("inf");
  char buf[48];
  const int len = std::snprintf(buf, sizeof(buf), "%.*f", digits, n);
  return (len > 0) ? write(reinterpret_cast<const uint8_t*>(buf),
                           static_cast<std::size_t>(len))
                   : 0U;
}

std::size_t Print::print(const __FlashStringHelper* s) {
  return print(reinterpret_cast<const char*>(s));
}

std::size_t Print::println() { return print("\r\n"); }
std::size_t Print::println(const char* s) { return print(s) + println(); }
std::size_t Print::println(char c) { return print(c) + println(); }
std::size_t Print::println(unsigned char b, int base) {
  return print(b, base) + println();
}
std::size_t Print::println(int n, int base) {
  return print(n, base) + println();
}
std::size_t Print::println(unsigned int n, int base) {
  return print(n, base) + println();
}
std::size_t Print::println(long n, int base) {
  return print(n, base) + println();
}
std::size_t Print::println(unsigned long n, int base) {
  return print(n, base) + println();
}
std::size_t Print::println(double n, int digits) {
  return print(n, digits) + println();
}
std::size_t Print::println(const __FlashStringHelper* s) {
  return print(s) + println();
}
