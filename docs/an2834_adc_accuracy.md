# STM32 SAR ADC Accuracy & Input Modeling Reference (AN2834)

Extracted from [cd00211314-how-to-optimize-the-adc-accuracy-in-the-stm32-mcus-stmicroelectronics.pdf](file:///workspaces/self-balancing-cube/docs/cd00211314-how-to-optimize-the-adc-accuracy-in-the-stm32-mcus-stmicroelectronics.pdf) (AN2834 Rev 10, Oct 2024).

## 1. SAR Switched-Capacitor Sampling Stage (§2.1, §4.2.6, Fig. 26, Fig. 38)

During **Sample state (`t_SMPL`)**:
- The internal sampling switch (`S1`) **closes** to connect the internal sampling capacitor (`C_ADC` / `C_sh`) through internal multiplexer/switch resistance (`R_ADC` / `R_PAR`) to the external pin.
- External path elements: source impedance `R_AIN`, PCB + pad parasitic capacitance `C_PCB + C_IO` (or external filter `C_AIN`).

During **Hold / Bit Conversion state (`t_SAR`)**:
- `S1` **opens**, disconnecting the pin and freezing the voltage on `C_ADC` for successive-approximation bit comparisons.

## 2. External Buffer / Opamp Interaction (§4.2.7)

- Driving the ADC pin directly from an opamp/CSA output (such as `DRV8304` `SOx`) isolates `R_AIN` at DC, but **when `S1` closes**, the discharged (or differently charged) `C_ADC` creates a step load on the amplifier output.
- The amplifier's closed-loop bandwidth and output impedance determine how quickly it recovers from this charge-sharing transient within `t_SMPL`.
- AN2834 §4.2.7 notes that `t_SMPL` must be several times longer than the amplifier's response/propagation time.

## 3. Charge Kickback & Residual Charge (§4.4.2–4.4.4)

- After a previous conversion (especially when scanning multiple ADC channels), `C_ADC` retains residual voltage (worst case `0 V` or `V_REF+`).
- When `S1` closes at the start of `t_SMPL`, charge transfers between `C_ADC` and `C_PCB` + the driving source, producing a voltage spike on the ADC pin that must decay below `0.5 LSB` before `S1` opens.

## 4. Negative Injection Current (§3.2.9, §4.2.10)

- If any analog or adjacent I/O pin drops below `V_SS` (`V_AIN < V_SS`), negative injection current flows out of the pin and induces leakage into adjacent ADC channels, degrading accuracy.
- In motor-drive current sensing, inductive switching ringing on `SOx` or ground bounce must not pull the ADC pin below `-0.3 V`.
