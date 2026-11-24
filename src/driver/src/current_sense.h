#pragma once

#include <cstdint>

#include "common/base_classes/CurrentSense.h"
#include "stm32_hal.h"

namespace stfoc {

struct AdcChannelConfig {
  hal_adc_t adc_instance;
  hal_adc_channel_t channel;
  hal_gpio_t port;
  uint32_t pin;
};

struct StAdcCurrentSenseConfig {
  AdcChannelConfig phase_a;
  AdcChannelConfig phase_b;
  float shunt_resistance;  // Shunt resistance in Ohms.
  float amp_gain;          // Current sense amplifier gain in V/V.
  float v_ref = 3.3f;
  float cal_a = 1.0f;
  float cal_b = 1.0f;
  hal_adc_sampling_time_t sampling_time = HAL_ADC_SAMPLING_TIME_13CYCLES;
  /// Optional callback to enter/exit zero-current calibration mode (e.g.,
  /// DRV8304 CAL bit via SPI, or a test harness mid-rail stimulus).
  void (*set_cal_mode)(bool enable, void* ctx) = nullptr;
  void* cal_ctx = nullptr;
};

/// Phase current sensor using STM32C5 injected ADC conversions on phase A and
/// phase B, with phase C inferred as -(ia + ib).
///
/// No DMA is used: each ADC converts a single injected rank into its dedicated
/// JDR1 register, which getPhaseCurrents() reads directly.
class StAdcCurrentSense final : public CurrentSense {
 public:
  enum class Error : uint8_t {
    kOk = 0,
    kInvalidConfig,
    kClockConfigFailed,
    kGpioInitFailed,
    kAdcInitFailed,
    kAdcConfigFailed,
    kAdcCalibrationFailed,
    kAdcStartFailed,
    kOffsetCalibrationFailed,
    kNotInitialized,
    kTriggerConfigFailed,
    kTriggerEnableFailed,
    kTriggerDisableFailed,
  };

  explicit StAdcCurrentSense(const StAdcCurrentSenseConfig& config);
  ~StAdcCurrentSense();

  StAdcCurrentSense(const StAdcCurrentSense&) = delete;
  StAdcCurrentSense& operator=(const StAdcCurrentSense&) = delete;

  /// Initializes both ADCs and runs zero-current offset calibration if
  /// `set_cal_mode` is configured. Returns 0 (`Error::kOk`) on success, or a
  /// non-zero `Error` code on failure.
  int init() override;
  void deinit();

  /// Configures a timer compare unit and TRGO2 to trigger injected ADC
  /// conversions `delay_ns` nanoseconds after the center of the low-side
  /// MOSFET on-time (CNT == 0 in center-aligned PWM2 mode).
  ///
  /// The compare unit is configured in PWM2 mode with its pulse set to the
  /// corresponding number of timer counts after 0 (clamped to [1, ARR - 1]) so
  /// that a single rising edge on OCxREF occurs each PWM period while counting
  /// up.
  Error ConfigureTimerTrigger(hal_tim_handle_t* htim,
                              hal_tim_oc_compare_unit_t compare_unit,
                              uint32_t delay_ns);

  Error EnableTrigger();
  Error DisableTrigger();

  PhaseCurrent_s getPhaseCurrents() override;

  Error last_error() const { return last_error_; }
  static const char* ErrorToString(Error error);

  uint16_t raw_a() const;
  uint16_t raw_b() const;

  hal_adc_handle_t* adc_a_handle() { return &hadc_a_; }
  hal_adc_handle_t* adc_b_handle() { return &hadc_b_; }

 private:
  Error InitSingleAdc(hal_adc_handle_t* hadc, const AdcChannelConfig& ch);
  bool ApplyInjectedConfig(hal_adc_handle_t* hadc,
                           hal_adc_inj_trig_src_t trigger_src,
                           hal_adc_inj_trig_edge_t trigger_edge);
  Error CalibrateZeroOffsets();

  StAdcCurrentSenseConfig config_;
  hal_adc_handle_t hadc_a_{};
  hal_adc_handle_t hadc_b_{};
  hal_adc_inj_trig_src_t trigger_src_{HAL_ADC_INJ_TRIG_SOFTWARE};
  Error last_error_{Error::kOk};
  bool adc_a_initialized_{false};
  bool adc_b_initialized_{false};
  bool trigger_configured_{false};
  bool trigger_enabled_{false};
};

}  // namespace stfoc
