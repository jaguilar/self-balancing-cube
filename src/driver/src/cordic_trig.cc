#include "cordic_trig.h"

#include <cmath>
#include <cstdint>
#include <numbers>

#include "common/foc_utils.h"
#include "stm32_hal.h"
#include "stm32c5xx_ll_cordic.h"

namespace stfoc {

void InitCordicTrig() {
  HAL_RCC_CORDIC_EnableClock();
  LL_CORDIC_Config(CORDIC, LL_CORDIC_FUNCTION_COSINE,
                   LL_CORDIC_PRECISION_6_CYCLE, LL_CORDIC_SCALING_FACTOR_0,
                   LL_CORDIC_NBWRITE_1, LL_CORDIC_NBREAD_2,
                   LL_CORDIC_INWIDTH_32_BIT, LL_CORDIC_OUTWIDTH_32_BIT);
}

}  // namespace stfoc

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kInvTwoPi = 1.0f / kTwoPi;
constexpr float kRadToQ31 = 2147483648.0f / kPi;
constexpr float kQ31ToFloat = 1.0f / 2147483648.0f;

inline int32_t AngleToQ31(float a) {
  if (a >= 0.0f && a < kTwoPi) {
    if (a >= kPi) {
      a -= kTwoPi;
    }
  } else {
    a -= kTwoPi * std::floor((a + kPi) * kInvTwoPi);
  }
  const float scaled = a * kRadToQ31;
  if (scaled >= 2147483648.0f || scaled < -2147483648.0f) {
    return static_cast<int32_t>(0x80000000U);
  }
  return static_cast<int32_t>(scaled);
}

}  // namespace

float _sin(float a) {
  CORDIC->WDATA = static_cast<uint32_t>(AngleToQ31(a));
  (void)CORDIC->RDATA;  // cos
  const int32_t sin_q31 = static_cast<int32_t>(CORDIC->RDATA);
  return static_cast<float>(sin_q31) * kQ31ToFloat;
}

float _cos(float a) {
  CORDIC->WDATA = static_cast<uint32_t>(AngleToQ31(a));
  const int32_t cos_q31 = static_cast<int32_t>(CORDIC->RDATA);
  (void)CORDIC->RDATA;  // sin
  return static_cast<float>(cos_q31) * kQ31ToFloat;
}

void _sincos(float a, float* s, float* c) {
  CORDIC->WDATA = static_cast<uint32_t>(AngleToQ31(a));
  const int32_t cos_q31 = static_cast<int32_t>(CORDIC->RDATA);
  const int32_t sin_q31 = static_cast<int32_t>(CORDIC->RDATA);
  *c = static_cast<float>(cos_q31) * kQ31ToFloat;
  *s = static_cast<float>(sin_q31) * kQ31ToFloat;
}
