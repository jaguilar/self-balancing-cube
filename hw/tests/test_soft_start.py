#!/usr/bin/env python3
"""ngspice simulation test suite for the 5S LiPo IRF4905 soft-start circuit."""

from dataclasses import dataclass, field
from pathlib import Path
import sys

from ngspice_common import get_pyplot, run_ngspice, run_test_cli

TESTS_DIR = Path(__file__).resolve().parent
BASE_CIR = TESTS_DIR / "soft_start.cir"
V_BATT_MAX = 21.0  # 5S LiPo fully charged (5 * 4.2 V)
T_PLUG_S = 10e-6   # Battery plug-in time in soft_start_common.inc


@dataclass(frozen=True)
class SimMetrics:
    """Measured metrics extracted from an ngspice transient run."""

    i_peak_a: float
    p_peak_w: float
    e_sw_j: float
    t_resistive_ms: float
    t_5_ms: float | None
    t_95_ms: float | None
    v_sg_final_v: float
    v_bus_final_v: float


@dataclass(frozen=True)
class WaveformData:
    """Time-series waveforms extracted from ngspice wrdata."""

    time_ms: list[float]
    v_in: list[float]
    v_sg: list[float]
    v_sd: list[float]
    v_bus: list[float]
    i_in: list[float]
    p_mos: list[float]


@dataclass(frozen=True)
class SimResult:
    """Combined metrics and waveforms for a simulation run."""

    metrics: SimMetrics
    waveforms: WaveformData


@dataclass(frozen=True)
class TestCase:
    """Declarative specification for a soft-start simulation test case."""

    name: str
    plot_filename: str
    v_batt: float = V_BATT_MAX
    expect_pass: bool = True
    params: dict[str, str] = field(default_factory=dict)
    tran: str | None = None


TEST_CASES: tuple[TestCase, ...] = (
    TestCase(
        name="Nominal Soft-Start (5S Full 21.0V, C1=470nF)",
        plot_filename="soft_start_nominal.png",
    ),
    TestCase(
        name="Low Battery Soft-Start (5S Min 15.0V, C1=470nF)",
        plot_filename="soft_start_low_batt.png",
        v_batt=15.0,
        params={"V_BATT_MAX": "15.0"},
    ),
    TestCase(
        name="Extreme Fast Corner (21.0V, Vth=-2.0V, C1=282nF [-40%])",
        plot_filename="soft_start_corner_fast.png",
        params={"V_TH": "-2.0", "C_GATE": "282n"},
    ),
    TestCase(
        name="Extreme Slow Corner (15.0V, Vth=-4.0V, C1=564nF [+20%])",
        plot_filename="soft_start_corner_slow.png",
        v_batt=15.0,
        params={"V_BATT_MAX": "15.0", "V_TH": "-4.0", "C_GATE": "564n"},
    ),
    TestCase(
        name="Intentionally Failing: Oversized Cap (C1=10uF)",
        plot_filename="soft_start_failing.png",
        expect_pass=False,
        params={"C_GATE": "10u"},
        tran="100u 400m",
    ),
    TestCase(
        name="Intentionally Failing: Undersized Resistors (R1=100R, R2=150R)",
        plot_filename="soft_start_undersized_res.png",
        expect_pass=False,
        params={"R1_VAL": "100", "R2_VAL": "150"},
        tran="2u 5m",
    ),
)


WR_VARS = ("v(v_in)", "v(gate)", "v(batt_sw)", "v(batt_bus)", "i_in")


def run_ngspice_cir(
    cir_path: Path,
    v_batt: float = V_BATT_MAX,
    param_overrides: dict[str, str] | None = None,
    tran_spec: str | None = None,
) -> SimResult:
    """Run ngspice on a .cir file, capture waveform data via wrdata, and compute metrics."""
    cols = run_ngspice(
        cir_path,
        wr_vars=WR_VARS,
        param_overrides=param_overrides,
        tran_spec=tran_spec,
    )
    return _compute_metrics(cols, v_batt=v_batt)


def _compute_metrics(cols: dict[str, list[float]], v_batt: float = V_BATT_MAX) -> SimResult:
    """Compute soft-start metrics from parsed wrdata columns."""
    times = cols["time"]
    v_ins = cols["v(v_in)"]
    v_gates = cols["v(gate)"]
    v_sws = cols["v(batt_sw)"]
    v_buses = cols["v(batt_bus)"]
    i_ins = cols["i_in"]

    i_peak = max(i_ins)
    p_peak = 0.0
    e_sw = 0.0
    t_resistive = 0.0
    t_5: float | None = None
    t_95: float | None = None
    v_5 = 0.05 * v_batt
    v_95 = 0.95 * v_batt

    v_sgs: list[float] = []
    v_sds: list[float] = []
    p_moss: list[float] = []

    for idx, t in enumerate(times):
        v_sg = v_ins[idx] - v_gates[idx]
        v_sd = v_ins[idx] - v_sws[idx]
        p_inst = v_sd * i_ins[idx]
        v_sgs.append(v_sg)
        v_sds.append(v_sd)
        p_moss.append(p_inst)
        p_peak = max(p_peak, p_inst)

        if t_5 is None and v_buses[idx] >= v_5:
            t_5 = (t - T_PLUG_S) * 1e3
        if t_95 is None and v_buses[idx] >= v_95:
            t_95 = (t - T_PLUG_S) * 1e3

        if idx > 0:
            dt = t - times[idx - 1]
            p_prev = p_moss[idx - 1]
            e_sw += 0.5 * (p_prev + p_inst) * dt
            # Resistive/linear zone: MOSFET is conducting (>0.5 A) while V_SD > 0.5 V
            if i_ins[idx] > 0.5 and v_sd > 0.5:
                t_resistive += dt

    metrics = SimMetrics(
        i_peak_a=i_peak,
        p_peak_w=p_peak,
        e_sw_j=e_sw,
        t_resistive_ms=t_resistive * 1e3,
        t_5_ms=t_5,
        t_95_ms=t_95,
        v_sg_final_v=v_sgs[-1],
        v_bus_final_v=v_buses[-1],
    )
    waveforms = WaveformData(
        time_ms=[t * 1e3 for t in times],
        v_in=v_ins,
        v_sg=v_sgs,
        v_sd=v_sds,
        v_bus=v_buses,
        i_in=i_ins,
        p_mos=p_moss,
    )
    return SimResult(metrics=metrics, waveforms=waveforms)


def evaluate_metrics(m: SimMetrics, v_batt: float = V_BATT_MAX) -> list[str]:
    """Return a list of failure reasons; empty list means PASS."""
    failures: list[str] = []

    if m.i_peak_a > 50.0:
        failures.append(f"Peak inrush current too high: {m.i_peak_a:.2f} A > 50.0 A")
    if m.i_peak_a < 1.0:
        failures.append(f"Peak inrush current suspiciously low: {m.i_peak_a:.2f} A < 1.0 A")

    if m.t_resistive_ms > 7.0:
        failures.append(
            f"MOSFET resistive-zone dwell time too long: {m.t_resistive_ms:.2f} ms > 7.0 ms"
        )
    if m.p_peak_w > 400.0:
        failures.append(f"MOSFET peak power too high: {m.p_peak_w:.1f} W > 400.0 W")
    if m.e_sw_j > 0.30:
        failures.append(f"MOSFET switching energy too high: {m.e_sw_j * 1e3:.1f} mJ > 300.0 mJ")

    if m.t_95_ms is None:
        failures.append("+BATT never reached 95% of battery voltage")
    elif m.t_95_ms > 60.0:
        failures.append(f"Inrush time to 95% V_batt too long: {m.t_95_ms:.2f} ms > 60.0 ms")

    # >= 5.5 V ensures >= 1.5 V overdrive above worst-case |V_GS(th)| = 4.0 V;
    # <= 10.5 V verifies D1 (10 V Zener) clamps safely below +/-20 V V_GS(max).
    if not (5.5 <= m.v_sg_final_v <= 10.5):
        failures.append(f"Final V_SG out of expected [5.5, 10.5] V range: {m.v_sg_final_v:.2f} V")
    if m.v_bus_final_v < 0.99 * v_batt:
        failures.append(
            f"Final +BATT voltage too low: {m.v_bus_final_v:.2f} V < {0.99 * v_batt:.2f} V"
        )

    return failures


def print_report(label: str, m: SimMetrics, failures: list[str]) -> None:
    """Print a concise summary of simulation metrics and check results."""
    t_5_str = f"{m.t_5_ms:.2f} ms" if m.t_5_ms is not None else "N/A"
    t_95_str = f"{m.t_95_ms:.2f} ms" if m.t_95_ms is not None else "N/A"
    rise_str = (
        f"{m.t_95_ms - m.t_5_ms:.2f} ms"
        if m.t_5_ms is not None and m.t_95_ms is not None
        else "N/A"
    )
    status = "PASS" if not failures else "FAIL"
    print(f"=== {label} [{status}] ===")
    print(f"  Peak inrush current : {m.i_peak_a:.2f} A")
    print(f"  MOSFET peak power   : {m.p_peak_w:.1f} W")
    print(f"  MOSFET switch energy: {m.e_sw_j * 1e3:.1f} mJ")
    print(f"  Resistive-zone time : {m.t_resistive_ms:.2f} ms")
    print(f"  +BATT 5% / 95% time : {t_5_str} / {t_95_str} (rise: {rise_str})")
    print(f"  Final V_SG / +BATT  : {m.v_sg_final_v:.2f} V / {m.v_bus_final_v:.2f} V")
    for f in failures:
        print(f"  [!] {f}")


def save_waveform_plot(
    label: str,
    result: SimResult,
    failures: list[str],
    out_path: Path,
    v_batt: float = V_BATT_MAX,
) -> None:
    """Render a 4-panel waveform PNG for viewing in VS Code."""
    plt = get_pyplot()

    m = result.metrics
    w = result.waveforms
    status = "PASS" if not failures else "FAIL"

    fig, axes = plt.subplots(4, 1, figsize=(10, 9), sharex=True)
    fig.suptitle(f"{label} [{status}]", fontsize=13, fontweight="bold")

    ax = axes[0]
    ax.plot(w.time_ms, w.v_in, label="V_in (Battery)", color="#4c72b0", alpha=0.7)
    ax.plot(w.time_ms, w.v_bus, label="+BATT (Bus)", color="#2ca02c", linewidth=1.8)
    ax.plot(w.time_ms, w.v_sg, label="V_SG (Gate Drive)", color="#ff7f0e", linewidth=1.4)
    ax.axhline(
        0.95 * v_batt,
        color="#2ca02c",
        linestyle="--",
        alpha=0.5,
        label=f"95% V_batt ({0.95 * v_batt:.2f} V)",
    )
    ax.axhline(3.0, color="#ff7f0e", linestyle=":", alpha=0.6, label="|V_th| (3.0 V)")
    ax.set_ylabel("Voltage [V]")
    ax.legend(loc="right", fontsize=8, ncol=2)
    ax.grid(True, alpha=0.3)

    ax = axes[1]
    ax.plot(w.time_ms, w.i_in, label=f"I_in (peak {m.i_peak_a:.2f} A)", color="#d62728", linewidth=1.6)
    ax.set_ylabel("Current [A]")
    ax.legend(loc="upper right", fontsize=9)
    ax.grid(True, alpha=0.3)

    ax = axes[2]
    ax.plot(
        w.time_ms,
        w.p_mos,
        label=f"P_MOSFET (peak {m.p_peak_w:.1f} W, E={m.e_sw_j * 1e3:.1f} mJ)",
        color="#9467bd",
        linewidth=1.6,
    )
    ax.fill_between(w.time_ms, 0, w.p_mos, color="#9467bd", alpha=0.15)
    ax.set_ylabel("Power [W]")
    ax.legend(loc="upper right", fontsize=9)
    ax.grid(True, alpha=0.3)

    ax = axes[3]
    ax.plot(
        w.time_ms,
        w.v_sd,
        label=f"V_SD (resistive dwell: {m.t_resistive_ms:.2f} ms)",
        color="#8c564b",
        linewidth=1.5,
    )
    in_res = [i > 0.5 and v > 0.5 for i, v in zip(w.i_in, w.v_sd)]
    ax.fill_between(
        w.time_ms,
        0,
        w.v_sd,
        where=in_res,
        color="#d62728",
        alpha=0.2,
        label="Resistive zone (I > 0.5 A, V_SD > 0.5 V)",
    )
    ax.set_ylabel("V_SD [V]")
    ax.set_xlabel("Time [ms]")
    ax.legend(loc="upper right", fontsize=9)
    ax.grid(True, alpha=0.3)

    if m.t_95_ms is not None:
        t95_abs = m.t_95_ms + T_PLUG_S * 1e3
        for a in axes:
            a.axvline(t95_abs, color="#2ca02c", linestyle="--", alpha=0.4)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"  Saved plot: {out_path}")


def run_test_case(case: TestCase, plot_dir: Path | None) -> bool:
    """Run a single TestCase and return True if its pass/fail outcome matches expectations."""
    res = run_ngspice_cir(
        BASE_CIR,
        v_batt=case.v_batt,
        param_overrides=case.params,
        tran_spec=case.tran,
    )
    failures = evaluate_metrics(res.metrics, v_batt=case.v_batt)
    print_report(case.name, res.metrics, failures)

    if plot_dir is not None:
        save_waveform_plot(
            case.name, res, failures, plot_dir / case.plot_filename, v_batt=case.v_batt
        )

    passed = not failures
    if case.expect_pass and not passed:
        print(f"\nERROR: '{case.name}' failed checks unexpectedly!", file=sys.stderr)
        return False
    if not case.expect_pass and passed:
        print(f"\nERROR: '{case.name}' was expected to FAIL but passed!", file=sys.stderr)
        return False
    return True


def main() -> int:
    return run_test_cli(
        description="Run ngspice soft-start tests.",
        cases=TEST_CASES,
        run_case_fn=run_test_case,
        default_plot_dir=TESTS_DIR / "plots",
        success_msg="ALL CHECKS PASSED (all nominal/corner cases passed, all failing cases detected).",
    )


if __name__ == "__main__":
    sys.exit(main())
