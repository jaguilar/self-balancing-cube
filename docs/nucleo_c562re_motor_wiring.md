# NUCLEO-C562RE (MB2213) Motor Driver Wiring — CN7 & CN10

Wiring guide for the first motor spin test on the **STM32C5 Nucleo-64 (MB2213)** board, extracted from [UM3615](file:///workspaces/self-balancing-cube/docs/um3615-stm32c5-nucleo64-board-mb2213-stmicroelectronics.pdf) (Tables 16–18) and mapped to [motor_driver_48-pinout.csv](file:///workspaces/self-balancing-cube/src/driver/motor_driver_48-pinout.csv).

**Scope:** Motor PWM, DRV8304 control (`drv_en`, `drv_nfault`, `drv_nss`), phase A & B current sensing, and SPI2 only. FDCAN, USART1, ABI encoder (`enc_a`, `enc_b`, `enc_i`), brake/bus-voltage sensing (`brake_en`, `vm_sense`), `drv_coast`, and `led_data` are intentionally omitted.

---

## Wiring Summary

| Subsystem | Signal / Label | MCU Pin | Peripheral / Mode | Header & Pin | Notes |
| :--- | :--- | :---: | :--- | :---: | :--- |
| **Motor PWM** | `drv_pwm_a` | `PA8` | `TIM1_CH1` | **CN10-23** | Phase A PWM |
| **Motor PWM** | `drv_pwm_b` | `PA9` | `TIM1_CH2` | **CN10-21** | Phase B PWM |
| **Motor PWM** | `drv_pwm_c` | `PA11` | `TIM1_CH4` | **CN10-14** | Phase C PWM (shared with `USB_DM` via SB23) |
| **DRV8304 Control** | `drv_en` | `PB6` | `GPIO` | **CN10-3** | Gate driver enable |
| **DRV8304 Control** | `drv_nfault` | `PB7` | `GPIO` | **CN10-5** | Active-low fault input |
| **DRV8304 Control** | `drv_nss` | `PB4` | `GPIO` | **CN10-27** | DRV8304 SPI chip select |
| **DRV8304 Control** | `drv_cal` | **`PC7`** *(added)* | `GPIO` (out) | **CN10-4** | Hardware DRV8304 current-sense amplifier calibration (`CAL`) |
| **Current Sense** | `drv_sense_a` | `PA1` | `ADC1_IN1` | **CN7-30** | Phase A current sense |
| **Current Sense** | `drv_sense_b` | **`PC5`** *(remapped)* | **`ADC2_IN5`** | **CN7-36** | Remapped from `PA2` (`ADC2_IN2`) to avoid STLINK VCP conflict on `PA2` |
| **SPI2** | `SPI2_SCK` | `PA5` | `SPI2_SCK` | **CN10-11** | Shared with user LED LD1 via SB8 |
| **SPI2** | `SPI2_MISO` | `PA7` | `SPI2_MISO` | **CN10-15** | |
| **SPI2** | `SPI2_MOSI` | `PB15` | `SPI2_MOSI` | **CN10-26** | Also on CN10-37 (do not use both simultaneously) |
| **SPI2** | `enc_nss` | `PB1` | `SPI2_NSS` | **CN10-18** | Encoder SPI chip select (if needed) |
| **Power / Ground** | `GND` | — | Ground | **CN7-8, 19, 20, 22** / **CN10-9, 20** | Common ground reference |
| **Power / Ground** | `3V3` | — | 3.3 V output | **CN7-16** | Logic reference (if needed) |

---

## CN7 (Left ST morpho Header)

Physical top-down layout (odd pins on left, even pins on right). Active test signals are **bolded**.

| Wired Signal | MCU Pin | Odd | Even | MCU Pin | Wired Signal |
| :--- | :---: | :---: | :---: | :---: | :--- |
| — | `PC10` | 1 | 2 | `PC11` | — |
| — | `PC12` | 3 | 4 | `PD2` | — |
| — | `3V3` (ext in) | 5 | 6 | `5V_EXT` | — |
| — | `PH2_BOOT0` | 7 | 8 | `GND` | **GND** |
| — *(STLINK VCP TX)* | `PA2` | 9 | 10 | NC | — |
| — *(STLINK VCP RX)* | `PA3` | 11 | 12 | `IOREF` | — |
| — | `PA13` (SWDIO) | 13 | 14 | `NRST` | — |
| — | `PA14` (SWCLK) | 15 | 16 | `3V3` | **3V3** (out) |
| — | `PA15` | 17 | 18 | `5V` | — |
| **GND** | `GND` | 19 | 20 | `GND` | **GND** |
| — | `PE2` | 21 | 22 | `GND` | **GND** |
| — | `PC13` | 23 | 24 | `VIN` | — |
| — | `PC14` | 25 | 26 | NC | — |
| — | `PC15` | 27 | 28 | `PA0` | — *(enc_a, unused)* |
| — | `PH0` | 29 | **30** | **`PA1`** | **`drv_sense_a`** (`ADC1_IN1`) |
| — | `PH1` | 31 | 32 | `PA4` | — *(enc_i, unused)* |
| — | NC | 33 | 34 | `PB0` | — *(vm_sense, unused)* |
| — | `PC2` | 35 | **36** | **`PC5`** | **`drv_sense_b`** (`ADC2_IN5`, remapped) |
| — | `PC3` | 37 | 38 | `PC0` | — |

---

## CN10 (Right ST morpho Header)

Physical top-down layout (odd pins on left, even pins on right). Active test signals are **bolded**.

| Wired Signal | MCU Pin | Odd | Even | MCU Pin | Wired Signal |
| :--- | :---: | :---: | :---: | :---: | :--- |
| — | NC | 1 | 2 | `PC8` | — |
| **`drv_en`** (`GPIO`) | **`PB6`** | **3** | **4** | **`PC7`** | **`drv_cal`** (`GPIO` out) |
| **`drv_nfault`** (`GPIO`) | **`PB7`** | **5** | 6 | `PB5` | — *(enc_b, unused)* |
| — | `VREFP` | 7 | 8 | `5V_STLINK` | — |
| **GND** | `GND` | 9 | 10 | NC | — |
| **`SPI2_SCK`** | **`PA5`** | **11** | 12 | `PA12` | — *(FDCAN1_TX, unused)* |
| — *(USART1_TX, unused)* | `PA6` | 13 | **14** | **`PA11`** | **`drv_pwm_c`** (`TIM1_CH4`) |
| **`SPI2_MISO`** | **`PA7`** | **15** | 16 | `PB12` | — *(FDCAN1_RX, unused)* |
| — | `PC9` | 17 | **18** | **`PB1`** | **`enc_nss`** (`SPI2_NSS`) |
| — | `PC6` | 19 | 20 | `GND` | **GND** |
| **`drv_pwm_b`** (`TIM1_CH2`) | **`PA9`** | **21** | 22 | `PB2` | — |
| **`drv_pwm_a`** (`TIM1_CH1`) | **`PA8`** | **23** | 24 | NC | — |
| — *(led_data, unused)* | `PB10` | 25 | **26** | **`PB15`** | **`SPI2_MOSI`** |
| **`drv_nss`** (`GPIO`) | **`PB4`** | **27** | 28 | `PB14` | — |
| — | `PC1` | 29 | 30 | `PB13` | — |
| — | `PB3` (SWO) | 31 | 32 | `AGND` (`VREF-`) | — |
| — *(USART1_RX, unused)* | `PA10` | 33 | 34 | `PC4` | — |
| — | `PB14` | 35 | 36 | NC | — |
| *(duplicate of CN10-26)* | `PB15` | 37 | 38 | NC | — |

---

## Hardware & Solder Bridge Notes (UM3615 Table 18)

- **`drv_sense_b` remapped to `PC5` (`ADC2_IN5`, CN7-36):** In `motor_driver_48-pinout.csv`, `drv_sense_b` is on `PA2` (`ADC2_IN2`), which is shared with the STLINK-V3EC Virtual COM Port TX (`SB26` ON by default). `PC5` (`ADC2_IN5`) is a free pin on the 64-pin Nucleo with no onboard solder-bridge conflicts, located on CN7 near `drv_sense_a` (`PA1`, CN7-30). Update the Nucleo test build's ADC2 channel from `ADC2_IN2` to `ADC2_IN5`.
- **`PA5` (`SPI2_SCK`, CN10-11):** Shared with green user LED `LD1` via `SB8` (ON by default). If high-speed SPI clock integrity is affected, remove `SB8`.
- **`PA11` (`drv_pwm_c`, CN10-14):** Shared with the user USB Type-C `USB_DM` line via `SB23` (ON by default). Leave USB Type-C (`CN13`) unplugged during motor testing.
- **`PB15` (`SPI2_MOSI`):** Appears on both **CN10-26** and **CN10-37** (via `SB49`, ON by default). Wire to **CN10-26** and leave CN10-37 unconnected.

## Intentionally Excluded Signals

- **Brake & Bus Voltage Sensing:** `PA3` (`brake_en`, CN7-11), `PB0` (`vm_sense`, CN7-34)
- **DRV8304 Coast:** `PB8` (`drv_coast`) — not needed on this test driver board (low-side pins are wired high regardless of digital control; `PB8` is also not routed to CN7/CN10)
- **FDCAN1:** `PB12` (`FDCAN1_RX`, CN10-16), `PA12` (`FDCAN1_TX`, CN10-12)
- **USART1:** `PA6` (`USART1_TX`, CN10-13), `PA10` (`USART1_RX`, CN10-33)
- **ABI Encoder:** `PA0` (`enc_a`, CN7-28), `PB5` (`enc_b`, CN10-6), `PA4` (`enc_i`, CN7-32)
- **LED:** `PB10` (`led_data`, CN10-25)
