#include "battery_brake.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "stm32c5xx_ll_dac.h"

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

bool EnsureAdcDacKernelClock() {
  if (HAL_RCC_ADCDAC_GetKernelClkSource() != HAL_RCC_ADCDAC_CLK_SRC_HCLK ||
      HAL_RCC_ADCDAC_GetKernelClkPrescaler() != HAL_RCC_ADCDAC_PRESCALER4) {
    if (HAL_RCC_ADCDAC_SetConfigKernelClk(
            HAL_RCC_ADCDAC_CLK_SRC_HCLK, HAL_RCC_ADCDAC_PRESCALER4) != HAL_OK) {
      return false;
    }
  }
  return true;
}

}  // namespace

StBatteryBrake::StBatteryBrake(const StBatteryBrakeConfig& config)
    : config_(config),
      v_dda_v_(config.v_ref),
      brake_threshold_v_(config.brake_threshold_v) {}

StBatteryBrake::~StBatteryBrake() { Deinit(); }

const char* StBatteryBrake::ErrorToString(Error error) {
  switch (error) {
    case Error::kOk:
      return "kOk";
    case Error::kInvalidConfig:
      return "kInvalidConfig";
    case Error::kClockConfigFailed:
      return "kClockConfigFailed";
    case Error::kGpioInitFailed:
      return "kGpioInitFailed";
    case Error::kVddaCalibrationFailed:
      return "kVddaCalibrationFailed";
    case Error::kAdcInitFailed:
      return "kAdcInitFailed";
    case Error::kAdcConfigFailed:
      return "kAdcConfigFailed";
    case Error::kAdcCalibrationFailed:
      return "kAdcCalibrationFailed";
    case Error::kAdcStartFailed:
      return "kAdcStartFailed";
    case Error::kDacConfigFailed:
      return "kDacConfigFailed";
    case Error::kCompInitFailed:
      return "kCompInitFailed";
    case Error::kCompConfigFailed:
      return "kCompConfigFailed";
    case Error::kCompStartFailed:
      return "kCompStartFailed";
    case Error::kNotInitialized:
      return "kNotInitialized";
    case Error::kConversionStartFailed:
      return "kConversionStartFailed";
    default:
      return "kUnknown";
  }
}

float StBatteryBrake::DividerScale() const {
  return (config_.r_top_ohms + config_.r_bottom_ohms) / config_.r_bottom_ohms;
}

float StBatteryBrake::brake_pin_threshold_v() const {
  return brake_threshold_v_ / DividerScale();
}

float StBatteryBrake::NominalVrefIntV() {
  const uint16_t vrefint_cal = *LL_ADC_VREFINT_CAL_ADDR;
  const float cal_vref_v =
      static_cast<float>(LL_ADC_VREFINT_CAL_VREF) / 1000.0f;
  return (static_cast<float>(vrefint_cal) * cal_vref_v) / 4096.0f;
}

bool StBatteryBrake::MeasureVddaV(float& vdda_v,
                                  hal_adc_handle_t* shared_adc1) {
  if (!EnsureAdcDacKernelClock()) {
    return false;
  }

  hal_adc_handle_t local_adc1{};
  hal_adc_handle_t* hadc1 = shared_adc1;
  const bool owns_adc1 = (shared_adc1 == nullptr);
  bool was_active = false;

  if (owns_adc1) {
    HAL_RCC_ADC12_EnableClock();
    hadc1 = &local_adc1;
    if (HAL_ADC_Init(hadc1, HAL_ADC1) != HAL_OK) {
      return false;
    }

    hal_adc_config_t adc_cfg{};
    adc_cfg.resolution = HAL_ADC_RESOLUTION_12_BIT;
    adc_cfg.sampling_mode = HAL_ADC_SAMPLING_MODE_NORMAL;
    if (HAL_ADC_SetConfig(hadc1, &adc_cfg) != HAL_OK) {
      HAL_ADC_DeInit(hadc1);
      return false;
    }
  } else {
    if (hadc1->instance != HAL_ADC1) {
      return false;
    }
    was_active = (HAL_ADC_GetState(hadc1) == HAL_ADC_STATE_ACTIVE);
    if (was_active && HAL_ADC_Stop(hadc1) != HAL_OK) {
      return false;
    }
  }

  auto cleanup = [&]() {
    if (HAL_ADC_GetStateGroup(hadc1, HAL_ADC_GROUP_REGULAR) ==
        HAL_ADC_GROUP_STATE_ACTIVE) {
      (void)HAL_ADC_REG_StopConv(hadc1);
    }
    if (owns_adc1) {
      (void)HAL_ADC_Stop(hadc1);
      HAL_ADC_DeInit(hadc1);
    } else if (was_active &&
               HAL_ADC_GetState(hadc1) != HAL_ADC_STATE_ACTIVE) {
      (void)HAL_ADC_Start(hadc1);
    }
  };

  hal_adc_reg_config_t reg_cfg{};
  reg_cfg.trigger_src = HAL_ADC_REG_TRIG_SOFTWARE;
  reg_cfg.trigger_edge = HAL_ADC_REG_TRIG_EDGE_NONE;
  reg_cfg.sequencer_length = 1U;
  reg_cfg.sequencer_discont = HAL_ADC_REG_SEQ_DISCONT_DISABLE;
  reg_cfg.continuous = HAL_ADC_REG_CONV_SINGLE;
  reg_cfg.overrun = HAL_ADC_REG_OVR_DATA_OVERWRITTEN;
  if (HAL_ADC_REG_SetConfig(hadc1, &reg_cfg) != HAL_OK) {
    cleanup();
    return false;
  }

  hal_adc_channel_config_t ch_cfg{};
  ch_cfg.group = HAL_ADC_GROUP_REGULAR;
  ch_cfg.sequencer_rank = 1U;
  // 139 cycles @ 36 MHz ~= 3.86 us, meeting VREFINT sampling time requirements.
  ch_cfg.sampling_time = HAL_ADC_SAMPLING_TIME_139CYCLES;
  ch_cfg.input_mode = HAL_ADC_IN_SINGLE_ENDED;
  if (HAL_ADC_SetConfigChannel(hadc1, HAL_ADC_CHANNEL_VREFINT, &ch_cfg) !=
      HAL_OK) {
    cleanup();
    return false;
  }

  if (owns_adc1 && HAL_ADC_Calibrate(hadc1) != HAL_OK) {
    cleanup();
    return false;
  }

  if (HAL_ADC_Start(hadc1) != HAL_OK) {
    cleanup();
    return false;
  }

  constexpr uint32_t kSamples = 16U;
  uint64_t sum = 0U;
  for (uint32_t i = 0; i < kSamples; ++i) {
    if (HAL_ADC_REG_TrigNextConv(hadc1) != HAL_OK ||
        HAL_ADC_REG_PollForConv(hadc1, 10U) != HAL_OK) {
      cleanup();
      return false;
    }
    sum += static_cast<uint64_t>(HAL_ADC_REG_ReadConversionData(hadc1) & 0xFFFF);
  }

  cleanup();

  if (sum == 0U) {
    return false;
  }

  const float avg_raw = static_cast<float>(sum) / static_cast<float>(kSamples);
  const float cal_vref_v =
      static_cast<float>(LL_ADC_VREFINT_CAL_VREF) / 1000.0f;
  const float vrefint_cal = static_cast<float>(*LL_ADC_VREFINT_CAL_ADDR);
  vdda_v = (vrefint_cal * cal_vref_v) / avg_raw;
  return (vdda_v >= 2.0f && vdda_v <= 4.0f);
}

StBatteryBrake::Error StBatteryBrake::InitAdc(hal_adc_handle_t* shared_adc2) {
  if (shared_adc2 != nullptr) {
    if (shared_adc2->instance != config_.adc_instance) {
      return Error::kInvalidConfig;
    }
    hadc_ = shared_adc2;
    owns_adc_ = false;

    const bool was_active =
        (HAL_ADC_GetState(hadc_) == HAL_ADC_STATE_ACTIVE);
    if (was_active) {
      if (HAL_ADC_Stop(hadc_) != HAL_OK) {
        return Error::kAdcConfigFailed;
      }
    }

    hal_adc_reg_config_t reg_cfg{};
    reg_cfg.trigger_src = HAL_ADC_REG_TRIG_SOFTWARE;
    reg_cfg.trigger_edge = HAL_ADC_REG_TRIG_EDGE_NONE;
    reg_cfg.sequencer_length = 1U;
    reg_cfg.sequencer_discont = HAL_ADC_REG_SEQ_DISCONT_DISABLE;
    reg_cfg.continuous = HAL_ADC_REG_CONV_SINGLE;
    reg_cfg.overrun = HAL_ADC_REG_OVR_DATA_OVERWRITTEN;
    if (HAL_ADC_REG_SetConfig(hadc_, &reg_cfg) != HAL_OK) {
      return Error::kAdcConfigFailed;
    }

    hal_adc_channel_config_t ch_cfg{};
    ch_cfg.group = HAL_ADC_GROUP_REGULAR;
    ch_cfg.sequencer_rank = 1U;
    ch_cfg.sampling_time = config_.adc_sampling_time;
    ch_cfg.input_mode = HAL_ADC_IN_SINGLE_ENDED;
    if (HAL_ADC_SetConfigChannel(hadc_, config_.adc_channel, &ch_cfg) !=
        HAL_OK) {
      return Error::kAdcConfigFailed;
    }

    if (was_active) {
      if (HAL_ADC_Start(hadc_) != HAL_OK) {
        return Error::kAdcStartFailed;
      }
    }

    adc_initialized_ = true;
    return Error::kOk;
  }

  if (!EnsureAdcDacKernelClock()) {
    return Error::kClockConfigFailed;
  }

  EnableAdcClock(config_.adc_instance);

  hadc_ = &owned_adc_;
  owns_adc_ = true;

  if (HAL_ADC_Init(hadc_, config_.adc_instance) != HAL_OK) {
    return Error::kAdcInitFailed;
  }
  adc_initialized_ = true;

  hal_adc_config_t adc_cfg{};
  adc_cfg.resolution = HAL_ADC_RESOLUTION_12_BIT;
  adc_cfg.sampling_mode = HAL_ADC_SAMPLING_MODE_NORMAL;
  if (HAL_ADC_SetConfig(hadc_, &adc_cfg) != HAL_OK) {
    return Error::kAdcConfigFailed;
  }

  hal_adc_reg_config_t reg_cfg{};
  reg_cfg.trigger_src = HAL_ADC_REG_TRIG_SOFTWARE;
  reg_cfg.trigger_edge = HAL_ADC_REG_TRIG_EDGE_NONE;
  reg_cfg.sequencer_length = 1U;
  reg_cfg.sequencer_discont = HAL_ADC_REG_SEQ_DISCONT_DISABLE;
  reg_cfg.continuous = HAL_ADC_REG_CONV_SINGLE;
  reg_cfg.overrun = HAL_ADC_REG_OVR_DATA_OVERWRITTEN;
  if (HAL_ADC_REG_SetConfig(hadc_, &reg_cfg) != HAL_OK) {
    return Error::kAdcConfigFailed;
  }

  hal_adc_channel_config_t ch_cfg{};
  ch_cfg.group = HAL_ADC_GROUP_REGULAR;
  ch_cfg.sequencer_rank = 1U;
  ch_cfg.sampling_time = config_.adc_sampling_time;
  ch_cfg.input_mode = HAL_ADC_IN_SINGLE_ENDED;
  if (HAL_ADC_SetConfigChannel(hadc_, config_.adc_channel, &ch_cfg) != HAL_OK) {
    return Error::kAdcConfigFailed;
  }

  if (HAL_ADC_Calibrate(hadc_) != HAL_OK) {
    return Error::kAdcCalibrationFailed;
  }

  if (HAL_ADC_Start(hadc_) != HAL_OK) {
    return Error::kAdcStartFailed;
  }

  return Error::kOk;
}

bool StBatteryBrake::WriteDacForThresholdV(float threshold_v) {
  const float v_pin = threshold_v / DividerScale();
  if (v_pin <= 0.0f || v_pin >= v_dda_v_) {
    return false;
  }

  const long raw_code = std::lround((v_pin / v_dda_v_) * 4095.0f);
  const uint32_t code =
      static_cast<uint32_t>(std::clamp<long>(raw_code, 0L, 4095L));
  LL_DAC_ConvertData12RightAligned(DAC1, LL_DAC_CHANNEL_1, code);
  brake_threshold_v_ = threshold_v;
  return true;
}

StBatteryBrake::Error StBatteryBrake::InitDac() {
  if (config_.comp_input_minus != HAL_COMP_INPUT_MINUS_DAC1_CH1) {
    return Error::kOk;
  }

  if (!EnsureAdcDacKernelClock()) {
    return Error::kClockConfigFailed;
  }

  HAL_RCC_DAC1_EnableClock();

  LL_DAC_Disable(DAC1, LL_DAC_CHANNEL_1);
  LL_DAC_DisableTrigger(DAC1, LL_DAC_CHANNEL_1);
  // NORMAL + BUFFER_DISABLE + CONNECT_INTERNAL routes DAC1_CH1 strictly to
  // internal peripherals (COMP1_INM) and keeps external pin PA4 disconnected.
  LL_DAC_ConfigOutput(DAC1, LL_DAC_CHANNEL_1, LL_DAC_OUTPUT_MODE_NORMAL,
                      LL_DAC_OUTPUT_BUFFER_DISABLE,
                      LL_DAC_OUTPUT_CONNECT_INTERNAL);

  if (!WriteDacForThresholdV(config_.brake_threshold_v)) {
    return Error::kInvalidConfig;
  }

  LL_DAC_Enable(DAC1, LL_DAC_CHANNEL_1);
  dac_initialized_ = true;

  // Wait well beyond tWAKEUP (8 us) for internal DAC output settling.
  HAL_Delay(1U);
  return Error::kOk;
}

StBatteryBrake::Error StBatteryBrake::InitComp() {
  HAL_RCC_COMP12_EnableClock();

  if (HAL_COMP_Init(&hcomp_, config_.comp_instance) != HAL_OK) {
    return Error::kCompInitFailed;
  }
  comp_initialized_ = true;

  hal_comp_config_t comp_cfg{};
  comp_cfg.power_mode = config_.comp_power_mode;
  comp_cfg.input_plus = config_.comp_input_plus;
  comp_cfg.input_minus = config_.comp_input_minus;
  comp_cfg.input_hysteresis = config_.comp_hysteresis;
  comp_cfg.output_polarity = HAL_COMP_OUTPUT_POLARITY_NONINVERTED;
#if defined(USE_HAL_COMP_EXTI) && (USE_HAL_COMP_EXTI == 1)
  comp_cfg.output_trigger = HAL_COMP_OUTPUT_TRIG_NONE;
#endif
  if (HAL_COMP_SetConfig(&hcomp_, &comp_cfg) != HAL_OK) {
    return Error::kCompConfigFailed;
  }

  if (HAL_COMP_Start(&hcomp_) != HAL_OK) {
    return Error::kCompStartFailed;
  }
  comp_started_ = true;

  return Error::kOk;
}

StBatteryBrake::Error StBatteryBrake::Init(hal_adc_handle_t* shared_adc2,
                                           hal_adc_handle_t* shared_adc1) {
  last_error_ = Error::kOk;

  if (config_.r_top_ohms < 0.0f || config_.r_bottom_ohms <= 0.0f ||
      config_.v_ref <= 0.0f) {
    last_error_ = Error::kInvalidConfig;
    return last_error_;
  }

  v_dda_v_ = config_.v_ref;
  if (config_.calibrate_vdda) {
    float measured_vdda = 0.0f;
    if (!MeasureVddaV(measured_vdda, shared_adc1)) {
      last_error_ = Error::kVddaCalibrationFailed;
      return last_error_;
    }
    v_dda_v_ = measured_vdda;
  }

  // Configure shared analog input pin (COMP_INP + ADC).
  EnableGpioClock(config_.sense_port);
  hal_gpio_config_t sense_gpio{};
  sense_gpio.mode = HAL_GPIO_MODE_ANALOG;
  sense_gpio.pull = HAL_GPIO_PULL_NO;
  if (HAL_GPIO_Init(config_.sense_port, config_.sense_pin, &sense_gpio) !=
      HAL_OK) {
    last_error_ = Error::kGpioInitFailed;
    return last_error_;
  }

  // Optionally route comparator output to the configured brake enable pin.
  if (config_.enable_output_pin) {
    EnableGpioClock(config_.output_port);
    hal_gpio_config_t out_gpio{};
    out_gpio.mode = HAL_GPIO_MODE_ALTERNATE;
    out_gpio.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
    out_gpio.pull = HAL_GPIO_PULL_NO;
    out_gpio.speed = HAL_GPIO_SPEED_FREQ_VERY_HIGH;
    out_gpio.alternate = config_.output_af;
    if (HAL_GPIO_Init(config_.output_port, config_.output_pin, &out_gpio) !=
        HAL_OK) {
      last_error_ = Error::kGpioInitFailed;
      Deinit();
      return last_error_;
    }
  }

  const Error adc_err = InitAdc(shared_adc2);
  if (adc_err != Error::kOk) {
    last_error_ = adc_err;
    Deinit();
    return last_error_;
  }

  const Error dac_err = InitDac();
  if (dac_err != Error::kOk) {
    last_error_ = dac_err;
    Deinit();
    return last_error_;
  }

  const Error comp_err = InitComp();
  if (comp_err != Error::kOk) {
    last_error_ = comp_err;
    Deinit();
    return last_error_;
  }

  initialized_ = true;
  return Error::kOk;
}

void StBatteryBrake::Deinit() {
  if (comp_started_) {
    (void)HAL_COMP_Stop(&hcomp_);
    comp_started_ = false;
  }
  if (comp_initialized_) {
    HAL_COMP_DeInit(&hcomp_);
    comp_initialized_ = false;
  }

  if (dac_initialized_) {
    LL_DAC_Disable(DAC1, LL_DAC_CHANNEL_1);
    dac_initialized_ = false;
  }

  if (adc_initialized_) {
    if (hadc_ != nullptr &&
        HAL_ADC_GetStateGroup(hadc_, HAL_ADC_GROUP_REGULAR) ==
            HAL_ADC_GROUP_STATE_ACTIVE) {
      (void)HAL_ADC_REG_StopConv(hadc_);
    }
    if (owns_adc_ && hadc_ != nullptr) {
      (void)HAL_ADC_Stop(hadc_);
      HAL_ADC_DeInit(hadc_);
    }
    hadc_ = nullptr;
    owns_adc_ = false;
    adc_initialized_ = false;
  }

  HAL_GPIO_DeInit(config_.sense_port, config_.sense_pin);
  if (config_.enable_output_pin) {
    HAL_GPIO_DeInit(config_.output_port, config_.output_pin);
  }

  initialized_ = false;
}

StBatteryBrake::Error StBatteryBrake::SetBrakeThresholdV(float threshold_v) {
  if (!initialized_ || !dac_initialized_) {
    last_error_ = Error::kNotInitialized;
    return last_error_;
  }
  if (!WriteDacForThresholdV(threshold_v)) {
    last_error_ = Error::kInvalidConfig;
    return last_error_;
  }
  last_error_ = Error::kOk;
  return last_error_;
}

StBatteryBrake::Error StBatteryBrake::StartConversion() {
  if (!initialized_ || hadc_ == nullptr) {
    last_error_ = Error::kNotInitialized;
    return last_error_;
  }

  if (HAL_ADC_REG_TrigNextConv(hadc_) != HAL_OK) {
    last_error_ = Error::kConversionStartFailed;
    return last_error_;
  }

  last_error_ = Error::kOk;
  return last_error_;
}

uint16_t StBatteryBrake::ReadRaw() const {
  if (!initialized_ || hadc_ == nullptr) {
    return 0U;
  }
  return static_cast<uint16_t>(HAL_ADC_REG_ReadConversionData(hadc_) & 0xFFFF);
}

float StBatteryBrake::ReadPinVoltageV() const {
  const uint16_t raw = ReadRaw();
  return (static_cast<float>(raw) * v_dda_v_) / 4096.0f;
}

float StBatteryBrake::ReadVoltageV() const {
  return ReadPinVoltageV() * DividerScale();
}

bool StBatteryBrake::IsBrakeActive() const {
  if (!initialized_) {
    return false;
  }
  return HAL_COMP_GetOutputLevel(const_cast<hal_comp_handle_t*>(&hcomp_)) ==
         HAL_COMP_OUTPUT_LEVEL_HIGH;
}

}  // namespace stfoc
