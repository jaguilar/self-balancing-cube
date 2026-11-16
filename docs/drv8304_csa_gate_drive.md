# DRV8304 Gate Driver & Current-Shunt Amplifier Reference

Extracted from [drv8304.pdf](file:///workspaces/self-balancing-cube/docs/drv8304.pdf) (SLVSE39B, Revised July 2018).
Board part: `DRV8304SRHAR` (SPI variant, VQFN-40).

## 1. Absolute Maximum & Recommended Operating Ratings (§6.1, §6.3)

| Parameter | Min | Max | Unit | Notes |
|---|---:|---:|---|---|
| Supply voltage (`VM`, `VDRAIN`) | 6 | 38 | V | Recommended operating (`40 V` abs max) |
| Differential ground (`AGND`, `DGND`, `PGND`) | -0.5 | +0.5 | V | Abs max |
| Continuous shunt input (`SPx`, `SNx`) | -1 | +1 | V | Abs max |
| Pulsed 200 ns shunt input (`SPx`, `SNx`) | -2 | +2 | V | Abs max |
| Reference input (`VREF`) | -0.3 | 5.75 | V | Abs max (`VREFUV = 2.6 V`) |
| `SOx` output voltage range | -0.3 | `VREF` | V | Abs max |
| `SOx` output current (`I_SO`) | 0 | 5 | mA | Recommended operating |
| Applied PWM frequency (`f_PWM`) | — | 200 | kHz | Recommended max |

## 2. Smart Gate Drive Characteristics (§6.5, §7.3.1.4, §7.6.2)

- **Low-side gate drive (`V_GSL`)**: `7.5 V` min, `10 V` typ, `12.5 V` max (`V_VM = 12 to 38 V`, `I_LS_GATE = 0 to 15 mA`).
- **High-side gate drive (`V_GHS` w.r.t. `SHx`)**: `7 V` min, `10 V` typ, `11.5 V` max (`V_VM = 12 to 38 V`).
- **Propagation delay (`t_PD`)**: `INHx`/`INLX` transition to `GHx`/`GLx` transition: **180 ns typ, 250 ns max**.
- **SPI Dead Time (`t_DEAD`)**: `40 ns` (`00b`), `120 ns` (`01b`, default `100 ns` in Reg 0x05), `200 ns` (`10b`), `400 ns` (`11b`).
- **Peak Gate Drive Time (`t_DRIVE`)**: `500 ns` (`00b`), `1000 ns` (`01b`), `2000 ns` (`10b`), `4000 ns` (`11b`, default).
- **Strong Hold-Off Pulldown (`I_STRONG`)**: `300 mA` on opposite MOSFET gate during `t_DRIVE`.
- **Holding Current (`I_HOLD`)**: `15 mA` source / `30 mA` sink after `t_DRIVE`.
- **Gate Hold-Off Resistor (`R_OFF`)**: `150 kΩ` (`GHx` to `SHx`, `GLx` to `PGND`).

### `IDRIVE` Settings (SPI Reg 0x03 / 0x04)

| Code | `IDRIVEP` (Source) | `IDRIVEN` (Sink) |
|---|---:|---:|
| `000b` / `001b` | 15 mA | 30 mA |
| `010b` | 45 mA | 90 mA |
| `011b` | 60 mA | 120 mA |
| `100b` | 90 mA | 180 mA |
| `101b` | 105 mA | 210 mA |
| `110b` | 135 mA | 270 mA |
| `111b` (default) | 150 mA | 300 mA |

## 3. Current-Shunt Amplifier (CSA) Specifications (§6.5, §7.3.4, §8.2.1.2.4)

### Gain & Settling Time (`VREF = 3.3 to 5 V`)

| `CSA_GAIN` | Gain (`G_CSA`) Min / Typ / Max | Settling Time `t_SET` (±1%, 30 pF, 0.5 V step on `SOx`) |
|---|---|---:|
| `00b` | `4.85` / `5` / `5.15` V/V | **260 ns** typ |
| `01b` | `9.7` / `10` / `10.3` V/V | **400 ns** typ |
| `10b` | `19.4` / `20` / `20.6` V/V | **700 ns** typ |
| `11b` | `38.8` / `40` / `41.2` V/V | **1550 ns** typ |

### Input / Output Operating Limits (`VREF = 3.3 V`)

- **Common-mode input range (`V_SP,COM`)**: **-0.5 V to +0.5 V**.
- **Output linear range (`V_LINEAR`)**: **0.25 V to `V_VREF - 0.25 V`** (`0.25 V` to `3.05 V` at `VREF = 3.3 V`).
- **Output bias (`V_BIAS`)**:
  - Bidirectional (`VREF_DIV = 1b`, default): `V_VREF / 2` (`1.65 V` at `3.3 V`).
  - Unidirectional (`VREF_DIV = 0b`, SPI only): `V_VREF - 0.3 V` (`3.00 V` at `3.3 V`).
- **Transfer Equations (Eq. 3 & Eq. 4)** (inverting w.r.t. `V_SP - V_SN` when current flows `SP -> SN` to `PGND`):
  - Bidirectional: `V_SOx = V_VREF / 2 - G_CSA * I * R_SENSE`
  - Unidirectional: `V_SOx = (V_VREF - 0.3 V) - G_CSA * I * R_SENSE`
- **Input offset error (`V_OFF`, `VREF = 3.3 V ± 10%`)**:
  - `G = 5`: `±5 mV`
  - `G = 10`: `±2.5 mV`
  - `G = 20`: `±1.5 mV`
  - `G = 40`: `±1.25 mV`
  - Offset drift (`V_DRIFT`): `10 µV/°C` typ. (`AUTOCAL` calibrates offset on power-up or via SPI.)
- **Input bias current (`I_BIAS` on `SPx`/`SNx`)**: `200 µA` max (`V_SP = V_SN = 0 V`).

### Dynamic Range Check for `R_SENSE = 7 mΩ`, `VREF = 3.3 V`, `I = ±20 A`

- `|V_SENSE,max| = 20 A * 7 mΩ = 140 mV` (within `V_SP,COM = ±0.5 V`).
- Bidirectional (`V_BIAS = 1.65 V`, linear swing `±1.40 V`):
  - `G = 5`: `±0.70 V` → `SOx ∈ [0.95 V, 2.35 V]` (fits comfortably).
  - `G = 10`: `±1.40 V` → `SOx ∈ [0.25 V, 3.05 V]` (reaches exact `V_LINEAR` boundary at `±20 A`; `±16 A` gives `±1.12 V` → `[0.53 V, 2.77 V]`).
  - `G = 20`: `±2.80 V` → **saturates** `V_LINEAR` above `±10 A`.
  - `G = 40`: `±5.60 V` → **saturates** `V_LINEAR` above `±5 A`.
