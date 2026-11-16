#!/usr/bin/env python3
"""Shared ngspice netlist patching, execution, wrdata parsing, and CLI test runner utilities."""

import argparse
from collections.abc import Callable, Sequence
import os
from pathlib import Path
import re
import subprocess
import tempfile
from typing import Any

_INCLUDE_RE = re.compile(r"^(\.include\s+)(\S+)(.*)$")


def patch_netlist(
    cir_text: str,
    dat_path: Path,
    wr_vars: Sequence[str],
    param_overrides: dict[str, str] | None = None,
    tran_spec: str | None = None,
    include_dir: Path | None = None,
) -> str:
    """Apply parameter/.tran overrides, resolve relative .include paths, and inject wrdata."""
    overrides = param_overrides or {}
    updated: list[str] = []
    for line in cir_text.splitlines():
        stripped = line.strip()
        replaced = False
        for key, val in overrides.items():
            if stripped.startswith(f".param {key}") and (
                len(stripped) == len(f".param {key}")
                or stripped[len(f".param {key}")] in (" ", "\t", "=")
            ):
                updated.append(f".param {key} = {val}")
                replaced = True
                break
        if replaced:
            continue
        if tran_spec is not None and stripped.startswith(".tran "):
            updated.append(f".tran {tran_spec}")
            continue
        if include_dir is not None:
            m = _INCLUDE_RE.match(stripped)
            if m:
                inc_path = Path(m.group(2))
                if not inc_path.is_absolute():
                    updated.append(f"{m.group(1)}{include_dir / inc_path}{m.group(3)}")
                    continue
        updated.append(line)

    patched = "\n".join(updated)
    if ".endc" not in patched:
        raise RuntimeError("Netlist missing .endc block")
    wr_cmd = f"wrdata {dat_path} {' '.join(wr_vars)}\n.endc"
    return patched.replace(".endc", wr_cmd, 1)


def parse_wrdata(dat_path: Path, col_names: Sequence[str]) -> dict[str, list[float]]:
    """Parse ngspice wrdata output (with wr_singlescale) into a dict keyed by 'time' + col_names."""
    expected_cols = 1 + len(col_names)
    columns: dict[str, list[float]] = {"time": []}
    for name in col_names:
        columns[name] = []

    for line in dat_path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line[0].isalpha():
            continue
        parts = line.split()
        if len(parts) < expected_cols:
            continue
        columns["time"].append(float(parts[0]))
        for idx, name in enumerate(col_names):
            columns[name].append(float(parts[1 + idx]))

    if len(columns["time"]) < 2:
        raise RuntimeError(f"Insufficient waveform samples in {dat_path}")
    return columns


def run_ngspice(
    cir_path: Path,
    wr_vars: Sequence[str],
    param_overrides: dict[str, str] | None = None,
    tran_spec: str | None = None,
) -> dict[str, list[float]]:
    """Run ngspice in batch mode on a .cir file and return parsed wrdata waveforms."""
    cir_text = cir_path.read_text(encoding="utf-8")

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        (tmp / ".spiceinit").write_text("set ngbehavior=psa\n", encoding="utf-8")
        dat_path = tmp / "waveforms.dat"
        runner_cir = tmp / "runner.cir"
        runner_cir.write_text(
            patch_netlist(
                cir_text,
                dat_path,
                wr_vars=wr_vars,
                param_overrides=param_overrides,
                tran_spec=tran_spec,
                include_dir=cir_path.resolve().parent,
            ),
            encoding="utf-8",
        )

        proc = subprocess.run(
            ["ngspice", "-b", str(runner_cir)],
            cwd=tmp,
            capture_output=True,
            text=True,
            check=False,
        )
        if proc.returncode != 0:
            raise RuntimeError(
                f"ngspice failed (rc={proc.returncode}) on {cir_path}:\n"
                f"{proc.stdout}\n{proc.stderr}"
            )
        if not dat_path.exists():
            raise RuntimeError(f"ngspice did not produce {dat_path}")

        return parse_wrdata(dat_path, wr_vars)


def get_pyplot() -> Any:
    """Import and return matplotlib.pyplot configured for headless Agg rendering."""
    os.environ.setdefault("MPLCONFIGDIR", tempfile.gettempdir())
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    return plt


def run_test_cli(
    description: str,
    cases: Sequence[Any],
    run_case_fn: Callable[[Any, Path | None], bool],
    default_plot_dir: Path,
    success_msg: str,
) -> int:
    """Shared CLI entrypoint for ngspice test suites with --plot and --plot-dir flags."""
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument(
        "--plot",
        action="store_true",
        help="Generate PNG waveform plots for all test cases.",
    )
    parser.add_argument(
        "--plot-dir",
        type=Path,
        default=default_plot_dir,
        help=f"Directory to write waveform PNGs (default: {default_plot_dir}).",
    )
    args = parser.parse_args()

    plot_dir = args.plot_dir if args.plot else None
    results = [run_case_fn(case, plot_dir) for case in cases]
    if all(results):
        print(f"\n{success_msg}")
        return 0
    return 1
