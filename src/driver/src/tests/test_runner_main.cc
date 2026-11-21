#include <sys/time.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "CppUTest/CommandLineTestRunner.h"
#include "basic_stdio_core.h"
#include "board_config.h"
#include "mx_cortex_mpu.h"
#include "mx_cortex_nvic.h"
#include "mx_icache.h"
#include "mx_rcc.h"
#include "stm32_hal.h"
#include "stm32c5xx_ll_usart.h"

namespace {

// Patched post-link by stamp_elf.py with the 16-char SHA-256 prefix of the ELF.
alignas(4) extern const char kBuildStampRecord[28] __attribute__((used)) =
    "BUILDSTAMP:0000000000000000";

constexpr std::size_t kStampOffset = 11;  // strlen("BUILDSTAMP:")
constexpr std::size_t kStampLength = 16;

void ReadBuildStamp(char (&out)[kStampLength + 1]) {
  // Read through a volatile pointer so GCC/LTO cannot constant-fold the
  // unpatched placeholder literal.
  volatile const char* src = kBuildStampRecord + kStampOffset;
  for (std::size_t i = 0; i < kStampLength; ++i) {
    out[i] = src[i];
  }
  out[kStampLength] = '\0';
}

[[noreturn]] void ErrorLoop() {
  while (true) {
  }
}

bool InitMinimalSystem() {
  if (mx_cortex_mpu_init() != SYSTEM_OK) {
    return false;
  }
  if (HAL_Init() != HAL_OK) {
    return false;
  }
  if (mx_cortex_nvic_init() != SYSTEM_OK) {
    return false;
  }
  if (mx_icache_init() == nullptr) {
    return false;
  }
  if (HAL_ICACHE_Start(mx_icache_gethandle(), HAL_ICACHE_IT_NONE) != HAL_OK) {
    return false;
  }
  if (mx_rcc_init() != SYSTEM_OK) {
    return false;
  }
  return true;
}

void FlushUartAndReset(hal_uart_handle_t* huart) {
  std::fflush(stdout);
  std::fflush(stderr);
  auto* usart = reinterpret_cast<USART_TypeDef*>(static_cast<uintptr_t>(huart->instance));
  const uint32_t start = HAL_GetTick();
  while (LL_USART_IsActiveFlag_TC(usart) == 0U && (HAL_GetTick() - start) < 50U) {
  }
  HAL_Delay(20);
  NVIC_SystemReset();
}

void ClearUartErrors(hal_uart_handle_t* huart) {
  auto* usart = reinterpret_cast<USART_TypeDef*>(static_cast<uintptr_t>(huart->instance));
  LL_USART_ClearFlag_ORE(usart);
  LL_USART_ClearFlag_FE(usart);
  LL_USART_ClearFlag_NE(usart);
  LL_USART_ClearFlag_PE(usart);
}

std::size_t ReadCommandLine(hal_uart_handle_t* huart, char* buf, std::size_t max_len) {
  std::size_t len = 0;
  while (true) {
    uint8_t ch = 0;
    const hal_status_t status = HAL_UART_Receive(huart, &ch, 1U, 100U);
    if (status == HAL_TIMEOUT) {
      continue;
    }
    if (status != HAL_OK) {
      ClearUartErrors(huart);
      continue;
    }
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      if (len > 0) {
        buf[len] = '\0';
        return len;
      }
      continue;
    }
    if (len + 1 < max_len) {
      buf[len++] = static_cast<char>(ch);
    }
  }
}

void RunSuiteCommand(hal_uart_handle_t* huart, char* arg) {
  while (*arg == ' ') {
    ++arg;
  }
  std::size_t arg_len = std::strlen(arg);
  while (arg_len > 0 && arg[arg_len - 1] == ' ') {
    arg[--arg_len] = '\0';
  }
  if (arg_len == 0) {
    std::printf("ERROR missing suite name\r\n");
    return;
  }

  char* suite = arg;
  char* test_name = nullptr;
  if (char* dot = std::strchr(arg, '.'); dot != nullptr) {
    *dot = '\0';
    test_name = dot + 1;
  }

  char prog_name[] = "driver_tests";
  char verbose_flag[] = "-v";
  char group_flag[] = "-sg";
  char name_flag[] = "-sn";

  char* argv[7]{};
  int argc = 0;
  argv[argc++] = prog_name;
  argv[argc++] = verbose_flag;
  argv[argc++] = group_flag;
  argv[argc++] = suite;
  if (test_name != nullptr && *test_name != '\0') {
    argv[argc++] = name_flag;
    argv[argc++] = test_name;
  }
  argv[argc] = nullptr;

  const int failures = CommandLineTestRunner::RunAllTests(argc, argv);
  std::printf("\r\nRESULT %d\r\n", failures);
  FlushUartAndReset(huart);
}

}  // namespace

extern "C" {

int _gettimeofday(struct timeval* tv, void* /*tzvp*/) {
  if (tv != nullptr) {
    const uint32_t ms = HAL_GetTick();
    tv->tv_sec = static_cast<time_t>(ms / 1000U);
    tv->tv_usec = static_cast<suseconds_t>((ms % 1000U) * 1000U);
  }
  return 0;
}

#ifndef putwc
wint_t putwc(wchar_t, FILE*) { return 0; }
#endif
#ifndef getwc
wint_t getwc(FILE*) { return 0; }
#endif
#ifndef ungetwc
wint_t ungetwc(wint_t, FILE*) { return 0; }
#endif

}  // extern "C"

int main() {
  if (!InitMinimalSystem()) {
    ErrorLoop();
  }

  hal_uart_handle_t* huart = board::InitDebugUart();
  if (huart == nullptr) {
    ErrorLoop();
  }

  BASIC_STDIO_Init(huart);
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);

  char stamp[kStampLength + 1]{};
  ReadBuildStamp(stamp);
  std::printf("\r\nREADY %s\r\n", stamp);

  char cmd[128]{};
  while (true) {
    ReadCommandLine(huart, cmd, sizeof(cmd));
    const std::string_view cmd_view{cmd};

    if (cmd_view == "RESET") {
      FlushUartAndReset(huart);
    } else if (cmd_view.starts_with("RUN ")) {
      RunSuiteCommand(huart, cmd + 4);
    } else {
      std::printf("ERROR unknown command\r\n");
    }
  }
}
