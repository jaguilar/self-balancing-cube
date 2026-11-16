# STM32C562CE 12-Bit SAR ADC Reference

Extracted from [DS_stm32c562ce.pdf](file:///workspaces/self-balancing-cube/docs/DS_stm32c562ce.pdf) (DS14927 Rev 2, June 2026).
Board part: `STM32C562CEU6` (UFQFPN48).

## 1. 12-Bit ADC Characteristics (Table 54)

| Symbol | Parameter | Conditions | Min | Typ | Max | Unit |
|---|---|---|---:|---:|---:|---|
| `V_DDA` | Analog power supply | — | 2.70 | — | 3.6 | V |
| `V_REF+` | Positive reference voltage | — | 2.5 | — | `V_DDA` | V |
| `V_REF-` | Negative reference voltage | — | — | `V_SSA` | — | V |
| `f_ADC` | ADC clock frequency | `2.7 V ≤ V_DDA ≤ 3.6 V` | 8 | — | 36 | MHz |
| `f_S` | Sampling rate (slow channels) | 12-bit, `f_ADC = 36 MHz`, `SMP = 2.5`, `R_AIN = 47 Ω`, `C_PCB = 22 pF` | — | 2.25 | — | MSPS |
| `V_AIN` | Conversion voltage range | — | 0 | — | `V_REF+` | V |
| `R_AIN` | Max external input impedance | 12-bit, `T_J = 140 °C` | — | — | 110 | Ω |
| `R_AIN` | Max external input impedance | 12-bit, `T_J = 125 °C` | — | — | 610 | Ω |
| `C_ADC` | Internal sample-and-hold capacitor | — | — | **3** | — | pF |
| `t_LATR` | Trigger conversion latency | Synchronous clock | — | 3 | — | `1/f_ADC` |
| `t_LATR` | Trigger conversion latency | Asynchronous clock | 3 | — | 4 | `1/f_ADC` |
| `t_S` | Sampling time (`SMP`) | Programmable | 2.5 | — | 288.5 | `1/f_ADC` |
| `t_CONV` | Total conversion time | `N`-bit resolution | — | `t_S + 1.5 + N` | — | `1/f_ADC` |

### Sampling Time (`t_S`) at `f_ADC = 36 MHz` (Figure 23, 12-bit)

- `SMP = 2.5 cycles`: **69 ns** (`2.25 MSPS`)
- `SMP = 4.5 cycles`: **125 ns** (`2.00 MSPS`)
- `SMP = 7.5 cycles`: **208 ns** (`1.714 MSPS`)
- `SMP = 12.5 cycles`: **347 ns** (`1.385 MSPS`)
- `SMP = 24.5 cycles`: **681 ns** (`0.947 MSPS`)
- `SMP = 47.5 cycles`: **1319 ns** (`0.590 MSPS`)

### Internal Switch Resistance (`R_ADC`) Estimate

`DS_stm32c562ce.pdf` specifies `C_ADC = 3 pF` and validates `SMP = 2.5` (`69 ns`) at `f_ADC = 36 MHz`, `R_AIN = 47 Ω`, `C_PCB = 22 pF`, `T_J = 140 °C`, `V_DDA = 2.7 V`. For 12-bit `< 0.5 LSB` settling (`ln(2^13) ≈ 9.01` time constants), `(R_AIN + R_ADC) * C_ADC ≤ 69 ns / 9.01 ≈ 7.66 ns`, bounding worst-case `R_ADC` to `≤ ~2.5 kΩ` (typical ~`500 Ω – 1.5 kΩ`).

## 2. I/O Pad & Leakage Characteristics (Tables 15, 47, 48)

- **I/O pin capacitance (`C_IO`)**: `5 pF` typ.
- **Input leakage (`I_leak`)**: `±200 nA` max for `0 < V_IN ≤ V_DDXXX`.
- **Negative injection susceptibility (`I_INJ`)**: `V_IN < V_SS` (below `-0.3 V` abs max) injects negative current; datasheet failure threshold is `> 5 LSB` TUE or induced leakage on adjacent analog pins.

## 3. 12-Bit ADC Accuracy (Table 55, after calibration, `V_REF+ = 3.3 V`, `1 LSB = 0.8057 mV`)

| Symbol | Parameter | Typ | Max | Unit |
|---|---|---:|---:|---|
| `ET` | Total unadjusted error | 4 | 5.7 | LSB |
| `EO` | Offset error | ±2.5 | ±5 | LSB |
| `EG` | Gain error | 2.6 | 6 | LSB |
| `ED` | Differential linearity error | — | -1 / +1.3 | LSB |
| `EL` | Integral linearity error | ±2.5 | ±3 | LSB |
| `ENOB` | Effective number of bits | 10.7 | — | bits |
