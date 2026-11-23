#include "current_sense.h"

#include <algorithm>
#include <cassert>

namespace stfoc {
namespace {

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
#if defined(GPIOF)
    case HAL_GPIOF:
      HAL_RCC_GPIOF_EnableClock();
      break;
#endif
#if defined(GPIOG)
    case HAL_GPIOG:
      HAL_RCC_GPIOG_EnableClock();
      break;
#endif
    case HAL_GPIOH:
      HAL_RCC_GPIOH_EnableClock();
      break;
    default:
      assert(false && "Unsupported GPIO port");
      break;
  }
}

void EnableAdcClock(hal_adc_t instance) {
  switch (instance) {
    case HAL_ADC1:
    case HAL_ADC2:
      HAL_RCC_ADC12_EnableClock();
      break;
#if defined(ADC3)
    case HAL_ADC3:
      HAL_RCC_ADC3_EnableClock();
      break;
#endif
    default:
      assert(false && "Unsupported ADC instance");
      break;
  }
}

uint32_t GetTimerClockFreq(hal_tim_t instance) {
  switch (instance) {
    case HAL_TIM1:
    case HAL_TIM8:
    case HAL_TIM15:
    case HAL_TIM16:
    case HAL_TIM17:
      return HAL_RCC_GetPCLK2Freq();
    case HAL_TIM2:
    case HAL_TIM5:
    case HAL_TIM12:
      return HAL_RCC_GetPCLK1Freq();
    default:
      assert(false && "Unsupported timer instance");
      return 0U;
  }
}

bool MapCompareUnitToTrgo2(hal_tim_oc_compare_unit_t compare_unit,
                           hal_tim_trigger_output2_source_t& trgo2) {
  switch (compare_unit) {
    case HAL_TIM_OC_COMPARE_UNIT_1:
      trgo2 = HAL_TIM_TRGO2_OC1;
      return true;
    case HAL_TIM_OC_COMPARE_UNIT_2:
      trgo2 = HAL_TIM_TRGO2_OC2;
      return true;
    case HAL_TIM_OC_COMPARE_UNIT_3:
      trgo2 = HAL_TIM_TRGO2_OC3;
      return true;
    case HAL_TIM_OC_COMPARE_UNIT_4:
      trgo2 = HAL_TIM_TRGO2_OC4;
      return true;
    case HAL_TIM_OC_COMPARE_UNIT_5:
      trgo2 = HAL_TIM_TRGO2_OC5;
      return true;
    case HAL_TIM_OC_COMPARE_UNIT_6:
      trgo2 = HAL_TIM_TRGO2_OC6;
      return true;
    case HAL_TIM_OC_COMPARE_UNIT_7:
      trgo2 = HAL_TIM_TRGO2_OC7;
      return true;
    default:
      return false;
  }
}

bool MapTimerToAdcInjTrgo2(hal_tim_t instance,
                           hal_adc_inj_trig_src_t& trig_src) {
  switch (instance) {
    case HAL_TIM1:
      trig_src = HAL_ADC_INJ_TRIG_TIM1_TRGO2;
      return true;
    case HAL_TIM8:
      trig_src = HAL_ADC_INJ_TRIG_TIM8_TRGO2;
      return true;
    default:
      return false;
  }
}

bool SampleInjectedAverage(hal_adc_handle_t* hadc, uint32_t samples,
                           float& avg_counts) {
  uint64_t sum = 0;
  for (uint32_t i = 0; i < samples; ++i) {
    if (HAL_ADC_INJ_StartConv(hadc) != HAL_OK ||
        HAL_ADC_INJ_PollForConv(hadc, 10U) != HAL_OK) {
      return false;
    }
    const int32_t raw = HAL_ADC_INJ_ReadConversionDataRank(hadc, 1U);
    sum += static_cast<uint64_t>(raw & 0xFFFF);
  }
  avg_counts = static_cast<float>(sum) / static_cast<float>(samples);
  return true;
}

}  // namespace

StAdcCurrentSense::StAdcCurrentSense(const StAdcCurrentSenseConfig& config)
    : config_(config) {
  offset_ia = config_.v_ref * 0.5f;
  offset_ib = config_.v_ref * 0.5f;
  offset_ic = 0.0f;
  gain_a = config_.amp_gain;
  gain_b = config_.amp_gain;
  gain_c = config_.amp_gain;
}

StAdcCurrentSense::~StAdcCurrentSense() { deinit(); }

const char* StAdcCurrentSense::ErrorToString(Error error) {
  switch (error) {
    case Error::kOk:
      return "kOk";
    case Error::kInvalidConfig:
      return "kInvalidConfig";
    case Error::kClockConfigFailed:
      return "kClockConfigFailed";
    case Error::kGpioInitFailed:
      return "kGpioInitFailed";
    case Error::kAdcInitFailed:
      return "kAdcInitFailed";
    case Error::kAdcConfigFailed:
      return "kAdcConfigFailed";
    case Error::kAdcCalibrationFailed:
      return "kAdcCalibrationFailed";
    case Error::kAdcStartFailed:
      return "kAdcStartFailed";
    case Error::kOffsetCalibrationFailed:
      return "kOffsetCalibrationFailed";
    case Error::kNotInitialized:
      return "kNotInitialized";
    case Error::kTriggerConfigFailed:
      return "kTriggerConfigFailed";
    case Error::kTriggerEnableFailed:
      return "kTriggerEnableFailed";
    case Error::kTriggerDisableFailed:
      return "kTriggerDisableFailed";
    default:
      return "kUnknown";
  }
}

StAdcCurrentSense::Error StAdcCurrentSense::InitSingleAdc(
    hal_adc_handle_t* hadc, const AdcChannelConfig& ch) {
  EnableGpioClock(ch.port);
  EnableAdcClock(ch.adc_instance);

  hal_gpio_config_t gpio_cfg{};
  gpio_cfg.mode = HAL_GPIO_MODE_ANALOG;
  gpio_cfg.pull = HAL_GPIO_PULL_NO;
  if (HAL_GPIO_Init(ch.port, ch.pin, &gpio_cfg) != HAL_OK) {
    return Error::kGpioInitFailed;
  }

  if (HAL_ADC_Init(hadc, ch.adc_instance) != HAL_OK) {
    return Error::kAdcInitFailed;
  }

  hal_adc_config_t adc_cfg{};
  adc_cfg.resolution = HAL_ADC_RESOLUTION_12_BIT;
  adc_cfg.sampling_mode = HAL_ADC_SAMPLING_MODE_NORMAL;
  if (HAL_ADC_SetConfig(hadc, &adc_cfg) != HAL_OK) {
    return Error::kAdcConfigFailed;
  }

  if (!ApplyInjectedConfig(hadc, HAL_ADC_INJ_TRIG_SOFTWARE,
                           HAL_ADC_INJ_TRIG_EDGE_NONE)) {
    return Error::kAdcConfigFailed;
  }

  hal_adc_channel_config_t ch_cfg{};
  ch_cfg.group = HAL_ADC_GROUP_INJECTED;
  ch_cfg.sequencer_rank = 1U;
  ch_cfg.sampling_time = config_.sampling_time;
  ch_cfg.input_mode = HAL_ADC_IN_SINGLE_ENDED;
  if (HAL_ADC_SetConfigChannel(hadc, ch.channel, &ch_cfg) != HAL_OK) {
    return Error::kAdcConfigFailed;
  }

  if (HAL_ADC_Calibrate(hadc) != HAL_OK) {
    return Error::kAdcCalibrationFailed;
  }

  if (HAL_ADC_Start(hadc) != HAL_OK) {
    return Error::kAdcStartFailed;
  }

  return Error::kOk;
}

bool StAdcCurrentSense::ApplyInjectedConfig(
    hal_adc_handle_t* hadc, hal_adc_inj_trig_src_t trigger_src,
    hal_adc_inj_trig_edge_t trigger_edge) {
  hal_adc_inj_config_t inj_cfg{};
  inj_cfg.trigger_src = trigger_src;
  inj_cfg.trigger_edge = trigger_edge;
  inj_cfg.sequencer_length = 1U;
  inj_cfg.sequencer_discont = HAL_ADC_INJ_SEQ_DISCONT_DISABLE;
  return HAL_ADC_INJ_SetConfig(hadc, &inj_cfg) == HAL_OK;
}

StAdcCurrentSense::Error StAdcCurrentSense::CalibrateZeroOffsets() {
  if (config_.set_cal_mode == nullptr) {
    return Error::kOk;
  }

  constexpr uint32_t kCalSamples = 64U;
  config_.set_cal_mode(true, config_.cal_ctx);

  float avg_a = 0.0f;
  float avg_b = 0.0f;
  const bool ok = SampleInjectedAverage(&hadc_a_, kCalSamples, avg_a) &&
                  SampleInjectedAverage(&hadc_b_, kCalSamples, avg_b);

  config_.set_cal_mode(false, config_.cal_ctx);

  if (!ok) {
    return Error::kOffsetCalibrationFailed;
  }

  const float counts_to_volts = config_.v_ref / 4096.0f;
  offset_ia = avg_a * counts_to_volts;
  offset_ib = avg_b * counts_to_volts;
  return Error::kOk;
}

int StAdcCurrentSense::init() {
  last_error_ = Error::kOk;

  if (config_.shunt_resistance <= 0.0f || config_.amp_gain <= 0.0f ||
      config_.v_ref <= 0.0f) {
    last_error_ = Error::kInvalidConfig;
    return static_cast<int>(last_error_);
  }

  // Configure ADCDAC kernel clock: HCLK (144 MHz) / 4 = 36 MHz.
  if (HAL_RCC_ADCDAC_GetKernelClkSource() != HAL_RCC_ADCDAC_CLK_SRC_HCLK ||
      HAL_RCC_ADCDAC_GetKernelClkPrescaler() != HAL_RCC_ADCDAC_PRESCALER4) {
    if (HAL_RCC_ADCDAC_SetConfigKernelClk(
            HAL_RCC_ADCDAC_CLK_SRC_HCLK, HAL_RCC_ADCDAC_PRESCALER4) != HAL_OK) {
      last_error_ = Error::kClockConfigFailed;
      return static_cast<int>(last_error_);
    }
  }

  const Error err_a = InitSingleAdc(&hadc_a_, config_.phase_a);
  if (err_a != Error::kOk) {
    last_error_ = err_a;
    deinit();
    return static_cast<int>(last_error_);
  }
  adc_a_initialized_ = true;

  const Error err_b = InitSingleAdc(&hadc_b_, config_.phase_b);
  if (err_b != Error::kOk) {
    last_error_ = err_b;
    deinit();
    return static_cast<int>(last_error_);
  }
  adc_b_initialized_ = true;

  const Error cal_err = CalibrateZeroOffsets();
  if (cal_err != Error::kOk) {
    last_error_ = cal_err;
    deinit();
    return static_cast<int>(last_error_);
  }

  trigger_src_ = HAL_ADC_INJ_TRIG_SOFTWARE;
  trigger_configured_ = false;
  trigger_enabled_ = false;
  initialized = true;
  return static_cast<int>(Error::kOk);
}

void StAdcCurrentSense::deinit() {
  if (trigger_enabled_) {
    if (adc_a_initialized_) {
      (void)HAL_ADC_INJ_StopConv(&hadc_a_);
    }
    if (adc_b_initialized_) {
      (void)HAL_ADC_INJ_StopConv(&hadc_b_);
    }
    trigger_enabled_ = false;
  }

  if (adc_a_initialized_) {
    (void)HAL_ADC_Stop(&hadc_a_);
    HAL_ADC_DeInit(&hadc_a_);
    adc_a_initialized_ = false;
  }
  if (adc_b_initialized_) {
    (void)HAL_ADC_Stop(&hadc_b_);
    HAL_ADC_DeInit(&hadc_b_);
    adc_b_initialized_ = false;
  }

  HAL_GPIO_DeInit(config_.phase_a.port, config_.phase_a.pin);
  HAL_GPIO_DeInit(config_.phase_b.port, config_.phase_b.pin);

  trigger_configured_ = false;
  initialized = false;
}

StAdcCurrentSense::Error StAdcCurrentSense::ConfigureTimerTrigger(
    hal_tim_handle_t* htim, hal_tim_oc_compare_unit_t compare_unit,
    uint32_t delay_ns) {
  if (!initialized) {
    last_error_ = Error::kNotInitialized;
    return last_error_;
  }
  if (htim == nullptr) {
    last_error_ = Error::kTriggerConfigFailed;
    return last_error_;
  }

  hal_tim_trigger_output2_source_t trgo2{};
  if (!MapCompareUnitToTrgo2(compare_unit, trgo2)) {
    last_error_ = Error::kTriggerConfigFailed;
    return last_error_;
  }

  hal_adc_inj_trig_src_t adc_trig{};
  if (!MapTimerToAdcInjTrgo2(htim->instance, adc_trig)) {
    last_error_ = Error::kTriggerConfigFailed;
    return last_error_;
  }

  const uint32_t tim_clk_hz = GetTimerClockFreq(htim->instance);
  if (tim_clk_hz == 0U) {
    last_error_ = Error::kTriggerConfigFailed;
    return last_error_;
  }

  const TIM_TypeDef* tim_regs =
      reinterpret_cast<const TIM_TypeDef*>(static_cast<uintptr_t>(htim->instance));
  const uint32_t prescaler = tim_regs->PSC;
  const uint32_t arr = tim_regs->ARR;
  if (arr < 2U) {
    last_error_ = Error::kTriggerConfigFailed;
    return last_error_;
  }

  const uint64_t counter_clk_hz =
      static_cast<uint64_t>(tim_clk_hz) / (static_cast<uint64_t>(prescaler) + 1ULL);
  const uint64_t raw_counts =
      (static_cast<uint64_t>(delay_ns) * counter_clk_hz + 500'000'000ULL) /
      1'000'000'000ULL;
  const uint32_t pulse = static_cast<uint32_t>(
      std::clamp<uint64_t>(raw_counts, 1ULL, static_cast<uint64_t>(arr - 1U)));

  hal_tim_oc_compare_unit_config_t oc_unit_cfg{};
  oc_unit_cfg.mode = HAL_TIM_OC_PWM2;
  oc_unit_cfg.pulse = pulse;
  if (HAL_TIM_OC_SetConfigCompareUnit(htim, compare_unit, &oc_unit_cfg) !=
          HAL_OK ||
      HAL_TIM_OC_EnableComparePreload(htim, compare_unit) != HAL_OK ||
      HAL_TIM_SetTriggerOutput2(htim, trgo2) != HAL_OK) {
    last_error_ = Error::kTriggerConfigFailed;
    return last_error_;
  }

  if (trigger_enabled_) {
    (void)HAL_ADC_INJ_StopConv(&hadc_a_);
    (void)HAL_ADC_INJ_StopConv(&hadc_b_);
    trigger_enabled_ = false;
  }

  trigger_src_ = adc_trig;
  trigger_configured_ = true;
  return EnableTrigger();
}

StAdcCurrentSense::Error StAdcCurrentSense::EnableTrigger() {
  if (!initialized) {
    last_error_ = Error::kNotInitialized;
    return last_error_;
  }
  if (!trigger_configured_) {
    last_error_ = Error::kTriggerConfigFailed;
    return last_error_;
  }
  if (trigger_enabled_) {
    last_error_ = Error::kOk;
    return last_error_;
  }

  if (!ApplyInjectedConfig(&hadc_a_, trigger_src_,
                           HAL_ADC_INJ_TRIG_EDGE_RISING) ||
      !ApplyInjectedConfig(&hadc_b_, trigger_src_,
                           HAL_ADC_INJ_TRIG_EDGE_RISING)) {
    last_error_ = Error::kTriggerEnableFailed;
    return last_error_;
  }

  if (HAL_ADC_INJ_StartConv(&hadc_a_) != HAL_OK ||
      HAL_ADC_INJ_StartConv(&hadc_b_) != HAL_OK) {
    last_error_ = Error::kTriggerEnableFailed;
    return last_error_;
  }

  trigger_enabled_ = true;
  last_error_ = Error::kOk;
  return last_error_;
}

StAdcCurrentSense::Error StAdcCurrentSense::DisableTrigger() {
  if (!initialized || !trigger_enabled_) {
    last_error_ = Error::kOk;
    return last_error_;
  }

  const bool ok_a = (HAL_ADC_INJ_StopConv(&hadc_a_) == HAL_OK);
  const bool ok_b = (HAL_ADC_INJ_StopConv(&hadc_b_) == HAL_OK);
  trigger_enabled_ = false;
  last_error_ = (ok_a && ok_b) ? Error::kOk : Error::kTriggerDisableFailed;
  return last_error_;
}

uint16_t StAdcCurrentSense::raw_a() const {
  return static_cast<uint16_t>(
      HAL_ADC_INJ_ReadConversionDataRank(&hadc_a_, 1U) & 0xFFFF);
}

uint16_t StAdcCurrentSense::raw_b() const {
  return static_cast<uint16_t>(
      HAL_ADC_INJ_ReadConversionDataRank(&hadc_b_, 1U) & 0xFFFF);
}

PhaseCurrent_s StAdcCurrentSense::getPhaseCurrents() {
  const uint16_t val_a = raw_a();
  const uint16_t val_b = raw_b();

  const float counts_to_volts = config_.v_ref / 4096.0f;
  const float volts_per_amp = config_.shunt_resistance * config_.amp_gain;

  const float ia =
      ((static_cast<float>(val_a) * counts_to_volts) - offset_ia) /
      volts_per_amp * config_.cal_a;
  const float ib =
      ((static_cast<float>(val_b) * counts_to_volts) - offset_ib) /
      volts_per_amp * config_.cal_b;
  const float ic = -(ia + ib);

  return {ia, ib, ic};
}

bool SampleInjectedAverage(hal_adc_handle_t* hadc, uint32_t samples,
                           float& avg_counts) {
  uint64_t sum = 0;
  for (uint32_t i = 0; i < samples; ++i) {
    if (HAL_ADC_INJ_StartConv(hadc) != HAL_OK ||
        HAL_ADC_INJ_PollForConv(hadc, 10U) != HAL_OK) {
      return false;
    }
    const int32_t raw = HAL_ADC_INJ_ReadConversionDataRank(hadc, 1U);
    sum += static_cast<uint64_t>(raw & 0xFFFF);
  }
  avg_counts = static_cast<float>(sum) / static_cast<float>(samples);
  return true;
}

}  // namespace stfoc
