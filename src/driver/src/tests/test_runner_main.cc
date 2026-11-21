#include <sys/time.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "CppUTest/CommandLineTestRunner.h"
#include "SEGGER_RTT.h"
#include "mx_cortex_mpu.h"
#include "mx_cortex_nvic.h"
#include "mx_icache.h"
#include "mx_rcc.h"
#include "rtt_syscalls.h"
#include "stm32_hal.h"

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

void FlushRttAndReset() {
  std::fflush(stdout);
  std::fflush(stderr);
  const uint32_t start = HAL_GetTick();
  while (SEGGER_RTT_GetBytesInBuffer(0U) > 0U && (HAL_GetTick() - start) < 500U) {
  }
  HAL_Delay(20);
  NVIC_SystemReset();
}

std::size_t ReadCommandLine(char* buf, std::size_t max_len) {
  std::size_t len = 0;
  while (true) {
    uint8_t ch = 0;
    if (SEGGER_RTT_Read(0U, &ch, 1U) == 0U) {
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

void RunSuiteCommand(char* arg) {
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
  FlushRttAndReset();
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

  RttSyscallsInit();
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);

  char stamp[kStampLength + 1]{};
  ReadBuildStamp(stamp);
  std::printf("\r\nREADY %s\r\n", stamp);

  char cmd[128]{};
  while (true) {
    ReadCommandLine(cmd, sizeof(cmd));
    const std::string_view cmd_view{cmd};

    if (cmd_view == "RESET") {
      FlushRttAndReset();
    } else if (cmd_view.starts_with("RUN ")) {
      RunSuiteCommand(cmd + 4);
    } else {
      std::printf("ERROR unknown command\r\n");
    }
  }
}
