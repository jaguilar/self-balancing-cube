---
name: cubemx-csv-to-kicad-symbol
description: Convert a CubeMX/CubeMX2 pinout CSV into a KiCad symbol in hw/project_symbols.kicad_sym (new symbol, name-only update, or replace). Use when asked to create or refresh an MCU symbol from a CubeMX pinout.
---

# CubeMX pinout CSV -> KiCad symbol

Script: `.claude/skills/cubemx-csv-to-kicad-symbol/cubemx_csv_to_kicad_symbol.py` (Python 3, stdlib only).

## Steps
1. Find the pinout CSV (CubeMX "Export pinout" CSV, e.g. `src/driver/motor_driver_48-pinout.csv`).
2. Get the **exact part number** (e.g. `STM32C562CEU6`). The CSV does not contain it. Use it if the user
   gave it; otherwise ask. Do not guess the package letters.
3. Run (from the repo root):
   ```
   python3 .claude/skills/cubemx-csv-to-kicad-symbol/cubemx_csv_to_kicad_symbol.py \
       src/driver/motor_driver_48-pinout.csv \
       --lib hw/project_symbols.kicad_sym --part STM32C562CEU6 --tag motor_driver
   ```
   Symbol name = `<PART>_<tag>` (matches the existing `STM32C552KET6_motor_driver`); omit `--tag` for the bare
   part name, or use `--name` for a fully custom one. `--dry-run` reports without writing.
4. Report the mode the script printed and any renamed pins. The script re-reads its output and checks balanced
   parentheses and that every package pin appears exactly once. It cannot open KiCad: tell the user the
   symbol has not been opened in KiCad.

## What it does
- Symbol missing -> appended. Symbol present with the same set of pin numbers -> **only pin names change**
  (keyed by pin number; order, geometry, alternates and properties untouched). Pass `--regenerate` to rebuild an
  existing symbol anyway (new layout); this discards hand edits, so back up the library first. Pin numbers differ, or pins that
  were stacked now have different names -> symbol is regenerated (the script says why).
- Power inputs (VDD*, VBAT, VREF+, VCAP*) on top; ground (VSS*, VREF-) on the bottom. Everything else is sorted
  by name (natural order: PA2 before PA10) and grouped by GPIO bank (PA, PB, ...; NRST is its own group). Each
  bank stays whole on one side, with one blank pin-pitch row between banks; the split falls on the bank boundary
  that balances the sides best. All pins included.
- Stacked pins on the top/bottom edge (e.g. VDD `[24,36,48]`) get N empty pin pitches (N = number count) to each
  neighbour, so their number text doesn't overlap the next pin's line.
- Several CSV rows sharing one package pin (LQFP32 pin 2 = PC14/PH0) become one pin: the configured row names it,
  the others become KiCad alternates. The CSV Signals column is also added as alternates
  (`--no-signal-alternates` to skip).
- Pins with identical final names are one stacked pin: number `[24,36,48]`.
- QFN packages (package letter `U`, e.g. `STM32C562CEU6`) automatically get an exposed pad pin (`N+1`, e.g. pin `49`
  on a 48-pin QFN) stacked onto `VSS` so it is implicitly connected to ground (`--epad` / `--no-epad` to override).
- Name = `PIN_(label)`, else `PIN_(signal)` (label wins). Text in parentheses in the CSV pin name
  (`PA13(JTMS/SWDIO)`) is dropped. Signals with their `[tag]` stripped; a bare `GPIO` signal is ignored;
  several signals are joined with `/`. Separator changes with `--sep`.
- Names are capped at 20 characters (`--max-name`) so the body width is fixed and relabeling never resizes it.
  Too long -> (1) if >= 9 characters fit inside the parentheses, cut the middle of the label with `...`
  (>= 3 kept at each end); (2) else drop the parenthetical; (3) if still too long, bank + pin number (`PB12`).
  The script prints every shortened name.
- Pin types: bidirectional, `NRST` input, power/ground `power_in`, `VCAP`/`VLX*`/`VFB*` passive.
- Body is a plain rectangle sized from the longest names (estimate; adjust in KiCad if needed).
- CRLF/LF line endings of the library are preserved.
