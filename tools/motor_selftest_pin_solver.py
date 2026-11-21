#!/usr/bin/env python3
"""
Nucleo-C562RE Motor Driver Self-Test Pin & Peripheral Constraint Solver.

Reads the motor driver 48-pin pinout CSV and the STM32C562RET6 LQFP64 JSON
descriptor, excludes DUT-occupied pins/peripherals and Nucleo-C562RE reserved
pins, and solves for a conflict-free self-test harness pin assignment.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


DEFAULT_CSV = Path("src/driver/motor_driver_48-pinout.csv")
DEFAULT_MCU_JSON = Path("docs/stm32c562ret6_lqfp64.json")
DEFAULT_EXCLUDED_PINS = {
    "PH2": "BOOT0",
    "PC14": "LSE OSC32_IN (32 kHz crystal on Nucleo)",
    "PC15": "LSE OSC32_OUT (32 kHz crystal on Nucleo)",
    "PC13": "User Button B1 on Nucleo",
    "PB8": "CAN FD transceiver on Nucleo (not on ST morpho header)",
    "PB9": "CAN FD transceiver on Nucleo (not on ST morpho header)",
}


def normalize_pin(raw: str) -> str:
    """Normalize a pin/pad name like 'PC14-OSC32_IN(OSC32_IN)' -> 'PC14'."""
    return raw.split("(")[0].split("-")[0].strip()


def load_dut_pinout(csv_path: Path) -> tuple[dict[str, dict[str, str]], set[str]]:
    """Return (dut_pins, dut_peripherals) from the CubeMX pinout CSV."""
    dut_pins: dict[str, dict[str, str]] = {}
    dut_peripherals: set[str] = set()

    with csv_path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            signals_raw = row["Signals"].strip()
            if not signals_raw:
                continue
            pin = normalize_pin(row["Pin"])
            label = row["Pin labels"].strip()
            dut_pins[pin] = {"signals": signals_raw, "label": label}

            for entry in signals_raw.split(","):
                sig = entry.strip().split()[0]
                if sig == "GPIO":
                    continue
                if "_" in sig:
                    dut_peripherals.add(sig.split("_")[0])

    return dut_pins, dut_peripherals


def load_mcu_json(json_path: Path) -> tuple[dict[str, dict[str, Any]], dict[str, Any]]:
    """Return (gpio_pins_by_name, raw_mcu_data) from the LQFP64 JSON."""
    with json_path.open(encoding="utf-8") as f:
        mcu = json.load(f)

    pins: dict[str, dict[str, Any]] = {}
    for p in mcu["pins"]:
        if not p.get("gpio"):
            continue
        name = normalize_pin(p["pad"])
        pins[name] = {
            "pin": p["pin"],
            "pad": p["pad"],
            "port": p["port"],
            "port_index": p["port_index"],
            "afs": p.get("alternate_functions", []),
            "analog": p.get("analog_functions", []),
            "dac": p.get("dac_channels", []),
        }
    return pins, mcu


@dataclass
class JumperAssignment:
    subsystem: str
    dut_pin: str
    dut_signal: str
    dut_label: str
    harness_pin: str
    harness_lqfp64_pin: int
    harness_peripheral: str
    harness_signal: str
    harness_af: str
    direction: str  # "DUT -> Harness", "Harness -> DUT", "Bidirectional"
    notes: str = ""


@dataclass
class SolveResult:
    assignments: list[JumperAssignment]
    harness_peripherals: set[str]
    abi_timer: str
    abi_port: str
    excluded_pins: dict[str, str]
    spare_pins: list[str]


def _find_af(pin_info: dict[str, Any], signal: str) -> str | None:
    for af in pin_info["afs"]:
        if af["signal"] == signal:
            return af["af"]
    return None


def solve_harness(
    dut_pins: dict[str, dict[str, str]],
    dut_peripherals: set[str],
    mcu_pins: dict[str, dict[str, Any]],
    mcu_data: dict[str, Any],
    excluded_pins: dict[str, str],
) -> SolveResult:
    """Solve for a conflict-free self-test harness pin and peripheral assignment."""
    free_pins = sorted(
        p for p in mcu_pins if p not in dut_pins and p not in excluded_pins
    )

    # 1. Candidate 3-channel timers for Motor PWM capture (TIM1_CH1/CH2/CH4 -> PA8, PA9, PA11)
    pwm_cap_candidates: list[tuple[str, list[tuple[str, str, str]]]] = []
    for tim_name, tim_info in sorted(mcu_data.get("timers", {}).items()):
        if tim_name in dut_peripherals or tim_info.get("type") != "tim":
            continue
        ch_options: dict[str, list[tuple[str, str]]] = {}
        for ch_idx in (1, 2, 3, 4):
            sig = f"{tim_name}_CH{ch_idx}"
            opts = []
            for p in free_pins:
                af = _find_af(mcu_pins[p], sig)
                if af:
                    opts.append((p, af))
            if opts:
                ch_options[sig] = opts
        if len(ch_options) >= 3:
            # Take first 3 channels that have distinct pins
            sigs = sorted(ch_options.keys())[:3]
            for p0, af0 in ch_options[sigs[0]]:
                for p1, af1 in ch_options[sigs[1]]:
                    if p1 == p0:
                        continue
                    for p2, af2 in ch_options[sigs[2]]:
                        if p2 in (p0, p1):
                            continue
                        pwm_cap_candidates.append(
                            (tim_name, [(sigs[0], p0, af0), (sigs[1], p1, af1), (sigs[2], p2, af2)])
                        )

    # 2. Candidate SPI slave peripherals (SCK, MISO, MOSI; NSS handled via EXTI + software NSS)
    spi_instances = sorted(
        {
            af["instance"]
            for p in free_pins
            for af in mcu_pins[p]["afs"]
            if af["instance"].startswith("SPI") and af["instance"] not in dut_peripherals
        }
    )
    spi_candidates: list[tuple[str, dict[str, tuple[str, str]]]] = []
    for spi in spi_instances:
        req = [f"{spi}_SCK", f"{spi}_MISO", f"{spi}_MOSI"]
        opts = {
            sig: [(p, _find_af(mcu_pins[p], sig)) for p in free_pins if _find_af(mcu_pins[p], sig)]
            for sig in req
        }
        if not all(opts.values()):
            continue
        for p_sck, af_sck in opts[req[0]]:
            for p_miso, af_miso in opts[req[1]]:
                if p_miso == p_sck:
                    continue
                for p_mosi, af_mosi in opts[req[2]]:
                    if p_mosi in (p_sck, p_miso):
                        continue
                    spi_candidates.append(
                        (
                            spi,
                            {
                                "SCK": (p_sck, af_sck),
                                "MISO": (p_miso, af_miso),
                                "MOSI": (p_mosi, af_mosi),
                            },
                        )
                    )

    # 3. Candidate 1-channel PWM timers for RC-filtered analog DACs
    #    (vm_sense PB0, drv_sense_a PA1, drv_sense_b PA2)
    pwm_dac_candidates: list[tuple[str, str, str, str]] = []
    for tim_name, tim_info in sorted(mcu_data.get("timers", {}).items()):
        if tim_name in dut_peripherals or tim_info.get("type") != "tim":
            continue
        for ch_idx in (1, 2):
            sig = f"{tim_name}_CH{ch_idx}"
            for p in free_pins:
                af = _find_af(mcu_pins[p], sig)
                if af:
                    pwm_dac_candidates.append((tim_name, sig, p, af))

    # 4. Free basic timer for DMA -> GPIO BSRR Encoder ABI synthesis
    basic_timers = sorted(
        t
        for t, info in mcu_data.get("timers", {}).items()
        if t not in dut_peripherals
        and info.get("configuration") == "Basic"
        and info.get("supports_dma")
    )
    if not basic_timers:
        raise RuntimeError("No free DMA-capable basic timer (e.g. TIM6/TIM7) available for ABI synthesis.")
    abi_timer = basic_timers[0]

    # Backtracking search across (pwm_cap, spi, 3x pwm_dac, abi_gpios, misc_gpios)
    for cap_tim, cap_chs in pwm_cap_candidates:
        used_p = {p for _, p, _ in cap_chs}
        used_periphs = {cap_tim, abi_timer}

        for spi_inst, spi_map in spi_candidates:
            spi_pins = {p for p, _ in spi_map.values()}
            if spi_pins & used_p:
                continue
            used_p2 = used_p | spi_pins
            used_periphs2 = used_periphs | {spi_inst}

            # Pick 3 distinct timers & pins for vm_sense, drv_sense_a, drv_sense_b
            avail_dacs = [
                c for c in pwm_dac_candidates
                if c[0] not in used_periphs2 and c[2] not in used_p2
            ]
            for i, dac_vm in enumerate(avail_dacs):
                for j, dac_ia in enumerate(avail_dacs):
                    if j == i or dac_ia[0] == dac_vm[0] or dac_ia[2] == dac_vm[2]:
                        continue
                    for k, dac_ib in enumerate(avail_dacs):
                        if (
                            k in (i, j)
                            or dac_ib[0] in (dac_vm[0], dac_ia[0])
                            or dac_ib[2] in (dac_vm[2], dac_ia[2])
                        ):
                            continue

                        used_p3 = used_p2 | {dac_vm[2], dac_ia[2], dac_ib[2]}
                        used_periphs3 = used_periphs2 | {dac_vm[0], dac_ia[0], dac_ib[0]}

                        remaining = [p for p in free_pins if p not in used_p3]

                        # Need 3 pins on the same GPIO port for ABI synthesis (TIM6/7 + DMA -> GPIOx->BSRR)
                        by_port: dict[str, list[str]] = {}
                        for p in remaining:
                            by_port.setdefault(mcu_pins[p]["port"], []).append(p)

                        abi_choice: tuple[str, list[str]] | None = None
                        for port in sorted(by_port, key=lambda pt: (-len(by_port[pt]), pt)):
                            if len(by_port[port]) >= 3:
                                # Sort by port_index for clean contiguous wiring
                                port_pins = sorted(by_port[port], key=lambda x: mcu_pins[x]["port_index"])
                                abi_choice = (port, port_pins[:3])
                                break

                        if not abi_choice:
                            continue

                        abi_port, abi_pins = abi_choice
                        used_p4 = used_p3 | set(abi_pins)
                        rem_after_abi = [p for p in remaining if p not in used_p4]

                        # Need 3 more GPIOs (or 4 if PB8/drv_coast is not excluded):
                        # 1x brake_en readback (GPIO/EXTI),
                        # 2x/3x DRV8304 control (drv_en, drv_nfault, and optionally drv_coast)
                        include_drv_coast = "PB8" not in excluded_pins
                        needed_gpios = 4 if include_drv_coast else 3
                        if len(rem_after_abi) < needed_gpios:
                            continue

                        brake_pin, drv_en_pin, drv_nfault_pin = rem_after_abi[:3]
                        drv_coast_pin = rem_after_abi[3] if include_drv_coast else None
                        spare_pins = rem_after_abi[needed_gpios:]

                        assignments: list[JumperAssignment] = []

                        def add(
                            subsystem: str,
                            dut_pin: str,
                            h_pin: str,
                            h_periph: str,
                            h_sig: str,
                            h_af: str,
                            direction: str,
                            notes: str = "",
                        ) -> None:
                            d_info = dut_pins[dut_pin]
                            assignments.append(
                                JumperAssignment(
                                    subsystem=subsystem,
                                    dut_pin=dut_pin,
                                    dut_signal=d_info["signals"],
                                    dut_label=d_info["label"],
                                    harness_pin=h_pin,
                                    harness_lqfp64_pin=mcu_pins[h_pin]["pin"],
                                    harness_peripheral=h_periph,
                                    harness_signal=h_sig,
                                    harness_af=h_af,
                                    direction=direction,
                                    notes=notes,
                                )
                            )

                        # Motor PWMs
                        for dut_p, (sig, h_p, h_af) in zip(["PA8", "PA9", "PA11"], cap_chs):
                            add("Motor PWM", dut_p, h_p, cap_tim, sig, h_af, "DUT -> Harness", "Input Capture + DMA")

                        # Encoder ABI
                        for dut_p, h_p, role in zip(["PA0", "PB5", "PA4"], abi_pins, ["ABI_A", "ABI_B", "ABI_I"]):
                            add(
                                "Encoder ABI",
                                dut_p,
                                h_p,
                                abi_timer,
                                f"GPIO_OUT ({role})",
                                "GPIO",
                                "Harness -> DUT",
                                f"{abi_timer} + DMA -> GPIO{abi_port}->BSRR",
                            )

                        # SPI Slave (NSS handled internally via EXTI on PB1/PB4 + software NSS)
                        add("SPI Comms", "PA5", spi_map["SCK"][0], spi_inst, f"{spi_inst}_SCK", spi_map["SCK"][1], "DUT -> Harness", "SPI Slave clock")
                        add("SPI Comms", "PA7", spi_map["MISO"][0], spi_inst, f"{spi_inst}_MISO", spi_map["MISO"][1], "Harness -> DUT", "SPI Slave MISO -> Master MISO")
                        add("SPI Comms", "PB15", spi_map["MOSI"][0], spi_inst, f"{spi_inst}_MOSI", spi_map["MOSI"][1], "DUT -> Harness", "Master MOSI -> SPI Slave MOSI")

                        # Battery sense, Current sense & Brake comparator
                        add(
                            "Battery / Brake",
                            "PB0",
                            dac_vm[2],
                            dac_vm[0],
                            dac_vm[1],
                            dac_vm[3],
                            "Harness -> DUT",
                            "Via external RC filter (e.g. 10k + 100nF)",
                        )
                        add("Battery / Brake", "PA3", brake_pin, "GPIO/EXTI", "GPIO_IN (brake_en)", "GPIO", "DUT -> Harness", "COMP1_OUT readback")
                        add(
                            "Current Sense",
                            "PA1",
                            dac_ia[2],
                            dac_ia[0],
                            dac_ia[1],
                            dac_ia[3],
                            "Harness -> DUT",
                            "Via external RC filter (e.g. 10k + 100nF)",
                        )
                        add(
                            "Current Sense",
                            "PA2",
                            dac_ib[2],
                            dac_ib[0],
                            dac_ib[1],
                            dac_ib[3],
                            "Harness -> DUT",
                            "Via external RC filter (e.g. 10k + 100nF)",
                        )

                        # DRV8304 control GPIOs
                        add("DRV8304 Control", "PB6", drv_en_pin, "GPIO", "GPIO_IN (drv_en)", "GPIO", "DUT -> Harness", "Verify gate-drive enable")
                        add("DRV8304 Control", "PB7", drv_nfault_pin, "GPIO", "GPIO_OUT (drv_nfault)", "GPIO", "Harness -> DUT", "Active-low fault injection")
                        if drv_coast_pin is not None:
                            add("DRV8304 Control", "PB8", drv_coast_pin, "GPIO", "GPIO_IN (drv_coast)", "GPIO", "DUT -> Harness", "Verify coast mode")

                        return SolveResult(
                            assignments=assignments,
                            harness_peripherals=used_periphs3,
                            abi_timer=abi_timer,
                            abi_port=abi_port,
                            excluded_pins=excluded_pins,
                            spare_pins=spare_pins,
                        )

    raise RuntimeError("No valid conflict-free pin assignment found under current constraints.")


def verify_solution(
    result: SolveResult,
    dut_pins: dict[str, dict[str, str]],
    dut_peripherals: set[str],
    mcu_pins: dict[str, dict[str, Any]],
) -> None:
    """Independently verify all pin, peripheral, AF, and port constraints."""
    harness_pins = [a.harness_pin for a in result.assignments]
    assert len(harness_pins) == len(set(harness_pins)), "Duplicate harness pin assigned!"

    dut_pin_set = set(dut_pins.keys())
    excluded_set = set(result.excluded_pins.keys())
    harness_set = set(harness_pins)

    assert not (harness_set & dut_pin_set), f"Overlap with DUT pins: {harness_set & dut_pin_set}"
    assert not (harness_set & excluded_set), f"Overlap with excluded pins: {harness_set & excluded_set}"
    assert not (result.harness_peripherals & dut_peripherals), (
        f"Peripheral conflict: {result.harness_peripherals & dut_peripherals}"
    )

    abi_ports = set()
    for a in result.assignments:
        assert a.dut_pin in dut_pins, f"Unknown DUT pin: {a.dut_pin}"
        assert a.harness_pin in mcu_pins, f"Unknown MCU pin: {a.harness_pin}"
        if a.harness_af != "GPIO":
            found_af = _find_af(mcu_pins[a.harness_pin], a.harness_signal)
            assert found_af == a.harness_af, (
                f"AF mismatch on {a.harness_pin} for {a.harness_signal}: expected {a.harness_af}, got {found_af}"
            )
        if a.subsystem == "Encoder ABI":
            abi_ports.add(mcu_pins[a.harness_pin]["port"])

    assert len(abi_ports) == 1, f"Encoder ABI pins must be on a single GPIO port, got: {abi_ports}"


def format_markdown(result: SolveResult, dut_peripherals: set[str]) -> str:
    lines = [
        "# Nucleo-C562RE Motor Driver Self-Test Wiring Report",
        "",
        "Generated by `tools/motor_selftest_pin_solver.py` from `src/driver/motor_driver_48-pinout.csv` and `docs/stm32c562ret6_lqfp64.json`.",
        "",
        "## Jumper Wiring Table",
        "",
        "| Subsystem | DUT Pin | DUT Label / Signal | Direction | Harness Pin (LQFP64) | Harness Peripheral / Signal | AF | Notes |",
        "| :--- | :---: | :--- | :---: | :---: | :--- | :---: | :--- |",
    ]

    for a in result.assignments:
        dut_desc = f"`{a.dut_label}` ({a.dut_signal})" if a.dut_label else a.dut_signal
        lines.append(
            f"| {a.subsystem} | `{a.dut_pin}` | {dut_desc} | {a.direction} | "
            f"`{a.harness_pin}` (pin {a.harness_lqfp64_pin}) | `{a.harness_peripheral}` / `{a.harness_signal}` | "
            f"`{a.harness_af}` | {a.notes} |"
        )

    lines.extend(
        [
            "",
            "## Additional Self-Test Notes",
            "",
            "### 1. Analog Sensing (`vm_sense`, `drv_sense_a`, `drv_sense_b`) & Brake Comparator RC Filters",
            "- Because `DAC1_OUT1` (`PA4`) is occupied by `enc_i` (`TIM5_ETR`), all three analog inputs (`vm_sense` on `PB0`, `drv_sense_a` on `PA1`, and `drv_sense_b` on `PA2`) are stimulated by independent free timer PWM outputs through passive RC low-pass filters on a breadboard.",
            "- **Recommended values per channel (for `C = 100 nF`):** `R = 10 kΩ`, `C = 100 nF` (3× resistors, 3× capacitors total).",
            "  - Cutoff frequency: $f_c = \\frac{1}{2\\pi R C} \\approx 159\\text{ Hz}$",
            "  - 99% settling time ($\\approx 4.6\\tau$): $\\approx 4.6\\text{ ms}$",
            "  - With a 100 kHz PWM carrier, peak-to-peak ripple at 50% duty cycle is $< 10\\text{ mV}$ (~3–4 LSBs on a 12-bit ADC), well-suited for sweeping `ADC1_IN1`, `ADC2_IN2`, `ADC2_IN6`, and testing the `COMP1` brake trip threshold.",
            "- This also leaves `DAC1` free internally if you later route it to `COMP1_INM`.",
            "",
            "### 2. FDCAN1 Self-Test",
            "- The STM32C562RET6 has only one FDCAN instance (`FDCAN1`, on `PA12`/`PB12`), so FDCAN communication should be verified using **FDCAN1 internal loopback mode** (no external jumpers required).",
            "",
            "### 3. SPI Chip-Selects (`enc_nss` / `drv_nss`) & `drv_coast` (`PB8`)",
            "- `enc_nss` (`PB1`) and `drv_nss` (`PB4`) do not need external jumpers: monitor them directly via EXTI on `PB1`/`PB4` and run `SPI3` in Software Slave Management mode (`SSM = 1`, `SSI = 0`).",
            "- `PB8` (`drv_coast`) is routed to the onboard CAN FD transceiver and is not exposed on the ST morpho header; verify coast mode in software via `GPIOB->ODR` (or remap `drv_coast` in the Nucleo test build).",
            "",
            "## Resource Summary",
            "",
            f"- **DUT Peripherals (reserved):** {', '.join(f'`{p}`' for p in sorted(dut_peripherals))}",
            f"- **Harness Peripherals (assigned):** {', '.join(f'`{p}`' for p in sorted(result.harness_peripherals))}",
            f"- **Nucleo-C562RE Excluded Pins:** {', '.join(f'`{p}` ({reason})' for p, reason in sorted(result.excluded_pins.items()))}",
            f"- **Remaining Spare GPIO Pins ({len(result.spare_pins)}):** {', '.join(f'`{p}`' for p in result.spare_pins) or 'None'}",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV, help="Path to motor driver pinout CSV")
    parser.add_argument("--mcu-json", type=Path, default=DEFAULT_MCU_JSON, help="Path to STM32C562RET6 LQFP64 JSON")
    parser.add_argument(
        "--exclude",
        nargs="*",
        default=None,
        help="Override default Nucleo-excluded pins (e.g. --exclude PH2 PC13 PC14 PC15)",
    )
    parser.add_argument("--markdown", type=Path, default=None, help="Write Markdown wiring report to this file")
    parser.add_argument("--json", action="store_true", help="Print machine-readable JSON to stdout")
    parser.add_argument("--verify", action="store_true", help="Run strict post-solve constraint verification")
    args = parser.parse_args()

    dut_pins, dut_peripherals = load_dut_pinout(args.csv)
    mcu_pins, mcu_data = load_mcu_json(args.mcu_json)

    if args.exclude is not None:
        excluded = {normalize_pin(p): "User-excluded" for p in args.exclude}
    else:
        excluded = dict(DEFAULT_EXCLUDED_PINS)

    result = solve_harness(dut_pins, dut_peripherals, mcu_pins, mcu_data, excluded)

    if args.verify:
        verify_solution(result, dut_pins, dut_peripherals, mcu_pins)

    if args.markdown:
        md = format_markdown(result, dut_peripherals)
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(md, encoding="utf-8")
        print(f"Wrote Markdown wiring report to {args.markdown}")

    if args.json:
        payload = {
            "assignments": [a.__dict__ for a in result.assignments],
            "harness_peripherals": sorted(result.harness_peripherals),
            "dut_peripherals": sorted(dut_peripherals),
            "excluded_pins": result.excluded_pins,
            "spare_pins": result.spare_pins,
        }
        print(json.dumps(payload, indent=2))
    elif not args.markdown:
        print(format_markdown(result, dut_peripherals))

    if args.verify:
        print("Verification passed: zero pin, peripheral, AF, or port conflicts.", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
