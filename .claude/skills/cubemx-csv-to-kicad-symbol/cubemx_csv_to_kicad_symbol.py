#!/usr/bin/env python3
"""Convert a CubeMX / CubeMX2 pinout CSV into a KiCad symbol.

Usage:
  cubemx_csv_to_kicad_symbol.py PINOUT.csv --lib hw/project_symbols.kicad_sym \
      --part STM32C562CEU6 [--tag motor_driver] [--name SYMBOL_NAME] [--dry-run]

Behaviour
  * Symbol not in the library        -> appended.
  * Symbol exists, same pin numbers  -> ONLY pin names are updated (by pin number);
                                        order, geometry and everything else are kept.
  * Symbol exists, pins differ       -> symbol is replaced.
Layout
  * Power inputs (VDD*, VBAT, VREF+, VCAP...) on top, ground (VSS*, VREF-) on bottom.
  * Everything else sorted by name (natural order), split evenly left / right.
  * Pins with identical final names are merged into one stacked pin, e.g. "[24,36,48]".
  * Pin name = <pin>_(<label>) or <pin>_(<signal>); label wins over signal.
Only the Python standard library is used. File line endings (CRLF/LF) are preserved.
"""
from __future__ import annotations

import argparse
import csv
import itertools
import math
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

GRID = 2.54          # mm, pin pitch
PIN_LEN = 5.08       # mm
CHAR_W = 1.0         # mm per character of 1.27 mm text (estimate, used for sizing)
FONT = 1.27
DEFAULT_SEP = "_"
MAX_NAME = 20        # max characters in a pin name; the body width is fixed from this
MIN_PAREN = 9        # fewest characters worth keeping inside the parentheses (3 + "..." + 3)
ELLIPSIS = "..."
CHANNEL = 6 * GRID   # fixed central channel reserved for the top/bottom pins

POWER_RE = re.compile(r"^(VDD\w*|VBAT|VREF\+|VCAP\w*|VLX\w*|VFB\w*)$", re.I)
GROUND_RE = re.compile(r"^(VSS\w*|VREF-|GND\w*)$", re.I)
PASSIVE_RE = re.compile(r"^(VCAP\w*|VLX\w*|VFB\w*)$", re.I)


# ----------------------------------------------------------------------------- helpers
def natural_key(s: str):
    return [int(t) if t.isdigit() else t.lower() for t in re.split(r"(\d+)", s)]


def fmt(x: float) -> str:
    s = f"{x:.3f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def q(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def unq(s: str) -> str:
    return s.replace('\\"', '"').replace("\\\\", "\\")


def fmt_numbers(nums: list[str]) -> str:
    nums = sorted(nums, key=natural_key)
    return nums[0] if len(nums) == 1 else "[" + ",".join(nums) + "]"


def expand_numbers(field_: str) -> list[str]:
    field_ = field_.strip()
    if not (field_.startswith("[") and field_.endswith("]")):
        return [field_]
    out: list[str] = []
    for item in field_[1:-1].split(","):
        item = item.strip()
        m = re.fullmatch(r"(\d+)-(\d+)", item)
        if m:
            out += [str(i) for i in range(int(m.group(1)), int(m.group(2)) + 1)]
        elif item:
            out.append(item)
    return out


def match_paren(t: str, i: int) -> int:
    depth, instr, j = 0, False, i
    while j < len(t):
        c = t[j]
        if instr:
            if c == "\\":
                j += 1
            elif c == '"':
                instr = False
        elif c == '"':
            instr = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return j
        j += 1
    raise ValueError("unbalanced parentheses")


# ----------------------------------------------------------------------------- CSV
@dataclass
class Pin:
    name: str
    numbers: list[str] = field(default_factory=list)
    group: str = "io"          # power | ground | io
    etype: str = "bidirectional"
    alts: list[str] = field(default_factory=list)   # KiCad alternate functions
    full: str = ""                                  # un-shortened name


def signals_of(row: dict) -> list[str]:
    sigs = []
    for s in (row.get("Signals") or "").split(","):
        s = re.sub(r"\s*\[[^\]]*\]\s*$", "", s).strip()
        if s and s.upper() != "GPIO" and s not in sigs:   # bare "GPIO" says nothing
            sigs.append(s)
    return sigs


def shorten_middle(s: str, n: int) -> str:
    """Cut s to n characters with an ellipsis in the middle (keeps >=3 chars at both ends when n >= 9)."""
    if len(s) <= n:
        return s
    keep = n - len(ELLIPSIS)
    lead = (keep + 1) // 2
    return s[:lead] + ELLIPSIS + s[len(s) - (keep - lead):]


def make_name(base: str, desc: str, sep: str, limit: int) -> str:
    full = f"{base}{sep}({desc})" if desc else base
    if len(full) <= limit:
        return full
    avail = limit - len(base) - len(sep) - 2              # room inside the parentheses
    if desc and avail >= MIN_PAREN:
        return f"{base}{sep}({shorten_middle(desc, avail)})"   # 1) shorten the label/signal
    if len(base) <= limit:
        return base                                       # 2) drop the parenthetical
    m = re.match(r"^P[A-Z]\d+", base)
    return m.group(0) if m else shorten_middle(base, limit)   # 3) bank + pin number only


def describe(row: dict) -> str:
    label = (row.get("Pin labels") or "").strip()
    if label:
        return label.replace(",", "/")
    return "/".join(signals_of(row))


def load_pins(csv_path: Path, sep: str, signal_alts: bool = True, max_name: int = MAX_NAME) -> list[Pin]:
    with open(csv_path, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    if not rows or "Position" not in rows[0] or "Pin" not in rows[0]:
        sys.exit(f"ERROR: {csv_path} does not look like a CubeMX pinout CSV (need Position/Pin columns)")
    # A package pin can appear on several rows (e.g. LQFP32 pin 2 = PC14 and PH0). Those become ONE
    # symbol pin: the configured row (has a label/signal) names it, the others become alternates.
    by_pos: dict[str, list[tuple[str, dict]]] = {}
    for r in rows:
        num = (r.get("Position") or "").strip()
        raw = (r.get("Pin") or "").strip()
        if num and raw:
            by_pos.setdefault(num, []).append((raw.split("(")[0].strip(), r))   # "PC14-OSC32_IN(OSC32_IN)" -> "PC14-OSC32_IN"
    merged: dict[str, Pin] = {}
    for num, items in by_pos.items():
        base, r = next((it for it in items if describe(it[1])), items[0])
        if GROUND_RE.match(base):
            group = "ground"
        elif POWER_RE.match(base):
            group = "power"
        else:
            group = "io"
        desc = describe(r)
        full = f"{base}{sep}({desc})" if desc else base
        name = make_name(base, desc, sep, max_name)
        alts = {b for b, r2 in items if r2 is not r}
        if signal_alts:
            for _, r2 in items:
                alts.update(signals_of(r2))
        alts -= {name, base, full}
        if name not in merged:
            if PASSIVE_RE.match(base):
                et = "passive"
            elif group != "io":
                et = "power_in"
            elif base.upper() == "NRST":
                et = "input"
            else:
                et = "bidirectional"
            merged[name] = Pin(name, [], group, et, full=full)
        merged[name].numbers.append(num)
        merged[name].alts = sorted(set(merged[name].alts) | alts)
    return list(merged.values())


# ----------------------------------------------------------------------------- generation
class Writer:
    def __init__(self):
        self.lines: list[str] = []
        self.d = 0

    def open(self, s):
        self.lines.append("\t" * self.d + s)
        self.d += 1

    def close(self):
        self.d -= 1
        self.lines.append("\t" * self.d + ")")

    def line(self, s):
        self.lines.append("\t" * self.d + s)


def effects(w: Writer, size: float, italic=False):
    w.open("(effects")
    w.open("(font")
    w.line(f"(size {fmt(size)} {fmt(size)})")
    if italic:
        w.line("(italic yes)")
    w.close()
    w.close()


def prop(w: Writer, key, value, x=0.0, y=0.0, size=FONT, hide=False, italic=False):
    w.open(f"(property {q(key)} {q(value)}")
    w.line(f"(at {fmt(x)} {fmt(y)} 0)")
    w.line("(show_name no)")
    w.line("(do_not_autoplace no)")
    if hide:
        w.line("(hide yes)")
    effects(w, size, italic)
    w.close()


def guess_package(part: str, npins: int):
    m = re.match(r"^(STM32[A-Z]\d)(\d\d)", part.upper())
    family = m.group(1) if m else "STM32"
    line = m.group(0) if m else part.upper()
    pkg_letter = re.match(r"^STM32[A-Z]\d\d\d\w\w(\w)\d", part.upper())
    pkg = {"T": "LQFP", "U": "QFN", "Y": "WLCSP"}.get(pkg_letter.group(1)) if pkg_letter else None
    return family, line, pkg


def spread(ps: list) -> list[float]:
    """x positions for a row of top/bottom pins. Stacked pins ([24,36,48]) print wide number text, so a pin
    with N numbers keeps N empty pin pitches between itself and each neighbour."""
    need = lambda p: len(p.numbers) + 1 if len(p.numbers) > 1 else 1
    xs = [0.0]
    for a, b in zip(ps, ps[1:]):
        xs.append(xs[-1] + GRID * max(need(a), need(b)))
    shift = xs[(len(ps) - 1) // 2] if ps else 0.0
    return [x - shift for x in xs]


def bank_of(p: Pin) -> str:
    m = re.match(r"^P([A-Z])\d+", p.name)
    return m.group(1) if m else p.name.split("_(")[0]   # non-GPIO pins (NRST...) are their own group


def build_symbol(name: str, part: str, pins: list[Pin], csv_name: str, max_name: int = MAX_NAME) -> tuple[str, str]:
    tops = sorted([p for p in pins if p.group == "power"], key=lambda p: natural_key(p.name))
    bots = sorted([p for p in pins if p.group == "ground"], key=lambda p: natural_key(p.name))
    io = sorted([p for p in pins if p.group == "io"], key=lambda p: (natural_key(p.name.split("_(")[0]), natural_key(p.name)))
    # Keep each GPIO bank (PA, PB, ...) whole on one side, one blank row between banks.
    # Split at the bank boundary that balances the two sides best.
    groups = [list(g) for _, g in itertools.groupby(io, key=bank_of)]
    side_rows = lambda gs: sum(len(g) for g in gs) + max(len(gs) - 1, 0)
    split = min(range(len(groups) + 1),
                key=lambda n: (max(side_rows(groups[:n]), side_rows(groups[n:])),
                               abs(side_rows(groups[:n]) - side_rows(groups[n:]))))
    lgroups, rgroups = groups[:split], groups[split:]
    left = [p for g in lgroups for p in g]
    right = [p for g in rgroups for p in g]
    rows = max(side_rows(lgroups), side_rows(rgroups), 1)

    longest = lambda ps: max((len(p.name) for p in ps), default=0)
    ch_w = max(CHANNEL, (max(len(tops), len(bots)) + 1) * GRID)          # central channel for top/bottom pins
    # Width is fixed by the name limit, not by the current names, so relabeling never resizes the symbol.
    need_w = 2 * max(max_name, longest(left), longest(right)) * CHAR_W + ch_w + GRID
    hw = max(GRID * math.ceil(need_w / 2 / GRID), 3 * GRID)
    txs, bxs = spread(tops), spread(bots)
    extent = max([abs(x) for x in txs + bxs], default=0.0)
    hw = max(hw, GRID * math.ceil((extent + 3 * GRID) / GRID))          # top/bottom pins must fit inside the body
    need_h = (longest(tops) + longest(bots)) * CHAR_W + 2 * GRID
    hh = GRID * max(math.ceil((rows + 1) / 2), math.ceil(need_h / 2 / GRID))

    w = Writer()
    w.open(f"(symbol {q(name)}")
    w.open("(pin_names")
    w.line("(offset 0.254)")
    w.close()
    for k, v in (("exclude_from_sim", "no"), ("in_bom", "yes"), ("on_board", "yes"),
                 ("in_pos_files", "yes"), ("duplicate_pin_numbers_are_jumpers", "no")):
        w.line(f"({k} {v})")
    family, line, pkg = guess_package(part, len(pins))
    npins = sum(len(p.numbers) for p in pins)
    prop(w, "Reference", "U", hw * 0.75, hh + GRID, 1.524)
    prop(w, "Value", part, hw * 0.75, -hh - GRID, 1.524)
    prop(w, "Footprint", "", hide=True, italic=True)
    prop(w, "Datasheet", "~", hide=True, italic=True)
    prop(w, "Description", f"{part} ({npins}-pin) pin assignments from CubeMX pinout {csv_name}", hide=True)
    prop(w, "ki_keywords", f"{family} {line} MCU", hide=True)
    if pkg:
        prop(w, "ki_fp_filters", f"*{pkg}*{npins}*", hide=True)

    w.open(f"(symbol {q(name + '_0_1')}")
    w.open("(rectangle")
    w.line(f"(start {fmt(-hw)} {fmt(hh)})")
    w.line(f"(end {fmt(hw)} {fmt(-hh)})")
    w.open("(stroke")
    w.line("(width 0)")
    w.line("(type default)")
    w.close()
    w.open("(fill")
    w.line("(type color)")
    w.line("(color 246 255 188 1)")
    w.close()
    w.close()
    w.close()

    def emit(p: Pin, x, y, ang):
        w.open(f"(pin {p.etype} line")
        w.line(f"(at {fmt(x)} {fmt(y)} {ang})")
        w.line(f"(length {fmt(PIN_LEN)})")
        w.open(f"(name {q(p.name)}")
        effects(w, FONT)
        w.close()
        w.open(f"(number {q(fmt_numbers(p.numbers))}")
        effects(w, FONT)
        w.close()
        for alt in p.alts:
            w.line(f"(alternate {q(alt)} {p.etype} line)")
        w.close()

    w.open(f"(symbol {q(name + '_1_1')}")
    for gs, x, ang in ((lgroups, -hw - PIN_LEN, 0), (rgroups, hw + PIN_LEN, 180)):
        row = 0
        for g in gs:
            for p in g:
                emit(p, x, hh - GRID * (row + 1), ang)
                row += 1
            row += 1                                   # blank row between banks
    for p, x in zip(tops, txs):
        emit(p, x, hh + PIN_LEN, 270)
    for p, x in zip(bots, bxs):
        emit(p, x, -hh - PIN_LEN, 90)
    w.close()
    w.line("(embedded_fonts no)")
    w.close()
    info = (f"{len(pins)} symbol pins ({npins} package pins): {len(left)} left, {len(right)} right ({len(groups)} bank groups), "
            f"{len(tops)} top, {len(bots)} bottom; body {fmt(2*hw)} x {fmt(2*hh)} mm")
    return "\n".join(w.lines), info


# ----------------------------------------------------------------------------- library editing
HEADER = '(kicad_symbol_lib\n\t(version 20251024)\n\t(generator "kicad_symbol_editor")\n\t(generator_version "10.0")\n)\n'
PIN_RE = re.compile(r"\(pin\s+\w+\s+\w+\s*\r?\n")
NAME_RE = re.compile(r'\(name "((?:[^"\\]|\\.)*)"')
NUM_RE = re.compile(r'\(number "((?:[^"\\]|\\.)*)"')


def find_symbol(text: str, name: str):
    m = re.search(r"\(symbol " + re.escape(q(name)), text)
    if not m:
        return None
    return m.start(), match_paren(text, m.start())


def try_update(text: str, name: str, name_by_number: dict[str, str]):
    """Rename pins in place. Returns (new_text, changes) or (None, reason)."""
    s0, e0 = find_symbol(text, name)
    sym = text[s0:e0 + 1]
    blocks = []
    for m in PIN_RE.finditer(sym):
        s = m.start()
        blk = sym[s:match_paren(sym, s) + 1]
        nm, nu = NAME_RE.search(blk), NUM_RE.search(blk)
        if not nm or not nu:
            return None, "could not parse an existing pin"
        blocks.append((s + nm.start(1), s + nm.end(1), unq(nm.group(1)), expand_numbers(unq(nu.group(1)))))
    existing = [n for b in blocks for n in b[3]]
    if len(existing) != len(name_by_number) or set(existing) != set(name_by_number):
        return None, f"pin numbers differ (symbol has {len(existing)} pins, CSV has {len(name_by_number)})"
    edits, changes = [], []
    for a, b, old, nums in blocks:
        new_names = {name_by_number[n] for n in nums}
        if len(new_names) != 1:
            return None, f"pins {nums} are stacked in the symbol but now have different names {sorted(new_names)}"
        new = new_names.pop()
        if new != old:
            edits.append((a, b, q(new)[1:-1]))
            changes.append((fmt_numbers(nums), old, new))
    for a, b, rep in sorted(edits, reverse=True):
        sym = sym[:a] + rep + sym[b:]
    return text[:s0] + sym + text[e0 + 1:], changes


def indent_block(sym_text: str, eol: str, first_indent: bool) -> str:
    lines = sym_text.split("\n")
    out = [("\t" if (i or first_indent) else "") + l for i, l in enumerate(lines)]
    return eol.join(out)


def verify(text: str, name: str, expected: set[str]):
    match_paren(text, 0)
    span = find_symbol(text, name)
    assert span, "symbol missing after write"
    sym = text[span[0]:span[1] + 1]
    got = []
    for m in PIN_RE.finditer(sym):
        blk = sym[m.start():match_paren(sym, m.start()) + 1]
        got += expand_numbers(unq(NUM_RE.search(blk).group(1)))
    assert len(got) == len(set(got)), "duplicate pin numbers in symbol"
    assert set(got) == expected, f"pin numbers mismatch: {sorted(set(got) ^ expected)}"


# ----------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", type=Path, help="CubeMX pinout CSV")
    ap.add_argument("--lib", type=Path, required=True, help=".kicad_sym library to create/update")
    ap.add_argument("--part", help="exact part number, e.g. STM32C562CEU6 (required)")
    ap.add_argument("--tag", help="suffix for the symbol name, e.g. motor_driver -> STM32C562CEU6_motor_driver")
    ap.add_argument("--name", help="full symbol name (overrides --part/--tag naming)")
    ap.add_argument("--sep", default=DEFAULT_SEP, help='text between pin name and "(label)" (default "_")')
    ap.add_argument("--max-name", type=int, default=MAX_NAME,
                    help=f"max pin-name length; longer names are shortened (default {MAX_NAME})")
    ap.add_argument("--no-signal-alternates", action="store_true",
                    help="don't add the CSV Signals column as KiCad alternate functions")
    ap.add_argument("--regenerate", action="store_true",
                    help="rebuild an existing symbol from scratch (new layout) instead of only updating pin names")
    ap.add_argument("--dry-run", action="store_true", help="report what would happen, write nothing")
    a = ap.parse_args()

    if not a.part:
        sys.exit("ERROR: --part is required (exact part number, e.g. STM32C562CEU6). "
                 "It cannot be inferred from the CSV - ask the user.")
    part = a.part.strip().upper()
    name = a.name or (f"{part}_{a.tag}" if a.tag else part)
    pins = load_pins(a.csv, a.sep, not a.no_signal_alternates, a.max_name)
    name_by_number = {n: p.name for p in pins for n in p.numbers}

    raw = a.lib.read_bytes().decode("utf-8") if a.lib.exists() else HEADER
    text = raw
    eol = "\r\n" if "\r\n" in raw else "\n"
    if eol == "\r\n":
        text = raw.replace("\r\n", "\n")

    new_text, info = build_symbol(name, part, pins, a.csv.name, a.max_name)
    for p in pins:
        if p.full != p.name:
            print(f"   shortened: {p.full} -> {p.name}")
    span = find_symbol(text, name)
    if span is None:
        mode = "added"
        idx = text.rstrip().rfind(")")
        text = text[:idx] + "\t" + indent_block(new_text, "\n", False) + "\n" + text[idx:]
        print(f"[{mode}] {name}: {info}")
    else:
        upd, res = (None, "--regenerate given") if a.regenerate else try_update(text, name, name_by_number)
        if upd is not None:
            mode, text = "updated names", upd
            print(f"[{mode}] {name}: {len(res)} pin name(s) changed, layout untouched")
            for nums, old, new in res:
                print(f"   pin {nums}: {old} -> {new}")
        else:
            mode = "replaced"
            text = text[:span[0]] + indent_block(new_text, "\n", False) + text[span[1] + 1:]
            print(f"[{mode}] {name}: {res}; regenerated. {info}")

    verify(text, name, set(name_by_number))
    out = text.replace("\n", eol)
    if a.dry_run:
        print("(dry run - nothing written)")
        return
    a.lib.write_bytes(out.encode("utf-8"))
    print(f"wrote {a.lib} (verified: balanced parens, all {len(name_by_number)} package pins present exactly once)")


if __name__ == "__main__":
    main()
