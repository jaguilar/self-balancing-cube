#pragma once

#include <cstdint>

#include "stm32_hal.h"

namespace stfoc {

struct StBatteryBrakeConfig {
  hal_adc_t adc_instance = HAL_ADC2;
  hal_adc_channel_t adc_channel = HAL_ADC_CHANNEL_6;
  hal_adc_sampling_time_t adc_sampling_time = HAL_ADC_SAMPLING_TIME_48CYCLES;

  hal_comp_t comp_instance = HAL_COMP1;
  hal_comp_input_plus_t comp_input_plus = HAL_COMP_INPUT_PLUS_IO2;
  hal_comp_input_minus_t comp_input_minus = HAL_COMP_INPUT_MINUS_DAC1_CH1;
  hal_comp_input_hysteresis_t comp_hysteresis = HAL_COMP_INPUT_HYSTERESIS_HIGH;
  hal_comp_power_mode_t comp_power_mode = HAL_COMP_POWER_MODE_HIGH_SPEED;

  // Shared analog input pin (PB0: COMP1_INP2 + ADC2_IN6).
  hal_gpio_t sense_port = HAL_GPIOB;
  uint32_t sense_pin = HAL_GPIO_PIN_0;

  // Comparator output pin (PA3: COMP1_OUT AF14 -> brake_en).
  // Set enable_output_pin = false on Nucleo-C562RE when PA3 is connected to
  // the ST-Link VCP RX bridge.
  bool enable_output_pin = true;
  hal_gpio_t output_port = HAL_GPIOA;
  uint32_t output_pin = HAL_GPIO_PIN_3;
  hal_gpio_af_t output_af = HAL_GPIO_AF_14;

  // Voltage divider resistors: V_pin = V_m * R_bottom / (R_top + R_bottom).
  // Defaults match hw/motor_driver/controller.kicad_sch (R23 = 8k25 0.1%,
  // R24 = 1k1 0.1%, 8.5:1 ratio -> 0-28.05 V full scale at 3.3 V V_DDA).
  float r_top_ohms = 8250.0f;
  float r_bottom_ohms = 1100.0f;

  // Target VM voltage at which COMP1 asserts brake_en (used when
  // comp_input_minus == HAL_COMP_INPUT_MINUS_DAC1_CH1).
  float brake_threshold_v = 22.0f;

  // Nominal V_DDA / V_REF+ in Volts. When calibrate_vdda is true, Init()
  // measures actual V_DDA via ADC1 VREFINT + factory VREFINT_CAL for accurate
  // DAC threshold and ADC voltage scaling.
  float v_ref = 3.3f;
  bool calibrate_vdda = true;
};

/// Default battery voltage sensing and brake comparator configuration matching
/// src/driver/motor_driver_48-pinout.csv and hw/motor_driver/controller.kicad_sch:
///   - vm_sense: PB0 -> COMP1_INP2 + ADC2_IN6 (8k25 / 1k1 0.1% divider)
///   - brake_en: PA3 -> COMP1_OUT (AF14)
///   - COMP1_INM: internal DAC1_CH1 (disconnected from PA4), default 22.0 V
constexpr StBatteryBrakeConfig DefaultBatteryBrakeConfig(
    float brake_threshold_v = 22.0f, float r_top_ohms = 8250.0f,
    float r_bottom_ohms = 1100.0f) {
  StBatteryBrakeConfig cfg{};
  cfg.brake_threshold_v = brake_threshold_v;
  cfg.r_top_ohms = r_top_ohms;
  cfg.r_bottom_ohms = r_bottom_ohms;
  return cfg;
}

/// Battery voltage (VM) ADC monitor and hardware brake-resistor comparator.
///
/// Uses a regular software-triggered single conversion on ADC2 (so it does not
/// conflict with injected current-sense conversions) and COMP1 comparing
/// vm_sense against internal DAC1_CH1 (calibrated via VREFINT) with hysteresis
/// to drive brake_en in hardware.
class StBatteryBrake {
 public:
  enum class Error : uint8_t {
    kOk = 0,
    kInvalidConfig,
    kClockConfigFailed,
    kGpioInitFailed,
    kVddaCalibrationFailed,
    kAdcInitFailed,
    kAdcConfigFailed,
    kAdcCalibrationFailed,
    kAdcStartFailed,
    kDacConfigFailed,
    kCompInitFailed,
    kCompConfigFailed,
    kCompStartFailed,
    kNotInitialized,
    kConversionStartFailed,
  };

  explicit StBatteryBrake(
      const StBatteryBrakeConfig& config = DefaultBatteryBrakeConfig());
  ~StBatteryBrake();

  StBatteryBrake(const StBatteryBrake&) = delete;
  StBatteryBrake& operator=(const StBatteryBrake&) = delete;

  /// Configures the VM sense GPIO, optional V_DDA calibration via ADC1 VREFINT,
  /// ADC2 regular conversion channel, internal DAC1_CH1 reference, and COMP1
  /// brake comparator.
  ///
  /// Pass `shared_adc2` when ADC2 is already initialized and owned by another
  /// driver (such as StAdcCurrentSense::adc_b_handle()). Similarly pass
  /// `shared_adc1` if ADC1 is already owned (e.g. adc_a_handle()) and
  /// `calibrate_vdda` is enabled.
  Error Init(hal_adc_handle_t* shared_adc2 = nullptr,
             hal_adc_handle_t* shared_adc1 = nullptr);
  void Deinit();

  /// Updates the hardware brake comparator trip threshold in Volts (requires
  /// comp_input_minus == HAL_COMP_INPUT_MINUS_DAC1_CH1).
  Error SetBrakeThresholdV(float threshold_v);

  /// Starts a single software-triggered regular ADC conversion for VM.
  /// Non-blocking; the result remains in the ADC regular data register until
  /// overwritten by the next conversion.
  Error StartConversion();

  /// Reads the latest VM conversion result from the ADC regular data register
  /// and returns the scaled bus voltage in Volts.
  float ReadVoltageV() const;

  /// Reads the latest unscaled pin voltage at vm_sense in Volts.
  float ReadPinVoltageV() const;

  /// Reads the raw 12-bit ADC regular conversion count.
  uint16_t ReadRaw() const;

  /// Returns true when the brake comparator output is high (vm_sense > ref).
  bool IsBrakeActive() const;

  /// Returns (r_top_ohms + r_bottom_ohms) / r_bottom_ohms.
  float DividerScale() const;

  /// Returns the configured VM brake threshold in Volts.
  float brake_threshold_v() const { return brake_threshold_v_; }

  /// Returns the target voltage at the vm_sense pin corresponding to
  /// brake_threshold_v().
  float brake_pin_threshold_v() const;

  /// Returns the active V_DDA reference voltage in Volts (measured via VREFINT
  /// if calibrate_vdda is enabled, otherwise config_.v_ref).
  float v_dda_v() const { return v_dda_v_; }

  /// Measures V_DDA in Volts using ADC1's internal VREFINT channel and the
  /// chip's factory VREFINT_CAL value.
  static bool MeasureVddaV(float& vdda_v,
                           hal_adc_handle_t* shared_adc1 = nullptr);

  /// Returns the factory-calibrated VREFINT voltage in Volts for this chip.
  static float NominalVrefIntV();

  bool initialized() const { return initialized_; }
  Error last_error() const { return last_error_; }
  static const char* ErrorToString(Error error);

 private:
  Error InitAdc(hal_adc_handle_t* shared_adc2);
  Error InitDac();
  Error InitComp();
  bool WriteDacForThresholdV(float threshold_v);

  StBatteryBrakeConfig config_;
  hal_adc_handle_t owned_adc_{};
  hal_adc_handle_t* hadc_{nullptr};
  bool owns_adc_{false};
  bool adc_initialized_{false};

  bool dac_initialized_{false};

  hal_comp_handle_t hcomp_{};
  bool comp_initialized_{false};
  bool comp_started_{false};

  float v_dda_v_{3.3f};
  float brake_threshold_v_{22.0f};
  bool initialized_{false};
  Error last_error_{Error::kOk};
};

}  // namespace stfoc
