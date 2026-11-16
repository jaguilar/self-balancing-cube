#!/usr/bin/env python3
"""ngspice simulation test suite for the 7 mOhm shunt + DRV8304 CSA + STM32C562CE SAR ADC chain.

What is checked (all numbers come from docs/drv8304_csa_gate_drive.md, docs/stm32c562_adc.md and the
BSC059N04LS G datasheet):
  * Sampling accuracy: C_ADC at the end of the sampling window vs. the ideal CSA output, plus the
    pin error over a +/-50 ns window around the end of sampling.
  * DRV8304 pin abs-max ratings, applied per pin and as *amplitude* limits (pulsed 200 ns rating
    versus continuous rating), with ABS_MAX_DERATE of margin: SPx/SNx, GLx, SHx, GHx-SHx, SOx.
  * Power-stage stress: FET V_DS overshoot and HS/LS cross-conduction at the commutation.
  * Numerical sanity: the sampled value must not droop after sampling (switch-leakage artifact), and
    worst-case cases are re-run with a 2.5x finer timestep to confirm the ring peaks converged.

Case construction: every netlist parameter that matters is a typed field of CurrentSenseCase. The
`.param` overrides sent to ngspice and the Python-side expectations (ideal output, sample timing)
are both generated from those fields, so they cannot drift apart.
"""

import bisect
from dataclasses import dataclass, field, replace
from pathlib import Path
import sys

from ngspice_common import get_pyplot, run_ngspice, run_test_cli

TESTS_DIR = Path(__file__).resolve().parent
BASE_CIR = TESTS_DIR / "current_sense.cir"

# ---------------------------------------------------------------------------------------------
# Limits
# ---------------------------------------------------------------------------------------------
V_REF = 3.3
LSB_V = V_REF / 4096.0  # 0.8057 mV per 12-bit LSB
MAX_CADC_ERR_V = 2.0 * LSB_V  # 2 LSB dynamic/ringing error budget at the end of sampling
MAX_JITTER_ERR_V = 4.0 * LSB_V  # 4 LSB across the +/-50 ns window around the end of sampling
MAX_HOLD_DROOP_LSB = 0.1  # C_ADC must hold its value after sampling (catches switch-leakage artifacts)
R_SENSE = 7e-3
V_LIN_MIN = 0.25
V_LIN_MAX = V_REF - 0.25  # 3.05 V
V_SP_COM_LINEAR_MAX = 0.50  # DRV8304 V_SP,COM linear common-mode limit during sampling
V_ADC_PIN_MIN = -0.30  # STM32C5 abs min before negative injection current

# Abs-max ratings are not operating limits: require this fraction of every rating.
ABS_MAX_DERATE = 0.9
T_PULSE_S = 200e-9  # "pulsed 200 ns" rating window

# DRV8304 absolute maximum ratings (datasheet SLVSE39B section 6.1)
SPSN_CONT_V = 1.0  # SPx/SNx continuous: -1..+1 V
SPSN_PULSED_V = 2.0  # SPx/SNx pulsed 200 ns: -2..+2 V
GLX_CONT_MIN_V = -1.0  # GLx continuous min (max 13.5 V)
GLX_PULSED_MIN_V = -5.0  # GLx pulsed 200 ns min
GLX_MAX_V = 13.5
SHX_CONT_MIN_V = -2.0  # SHx continuous min (max VM + 2 V)
SHX_PULSED_MIN_V = -5.0  # SHx pulsed 200 ns min
SHX_MAX_OVER_VM_V = 2.0
GHX_SHX_MIN_V = -0.3  # GHx with respect to SHx
GHX_SHX_MAX_V = 13.5
SOX_MIN_V = -0.3  # SOx abs range -0.3 .. VREF

# BSC059N04LS G
FET_VDS_MAX_V = 40.0
FET_VTH_MIN_V = 1.2  # HS gate must be below the minimum threshold when the LS starts conducting
FET_TT_TYP_S = 1.15e-9  # Qrr = 23 nC typ (datasheet) at I_F = 20 A
FET_TT_HOT_S = 2.3e-9  # 2x Qrr: allowance for hot junction / faster di/dt than the datasheet test

# Firmware-side timing choices (these are decided by the simulations below)
F_PWM_HZ = 20e3
T_WAIT_S = 3.5e-6  # 15/30 mA gate drive needs ~3.5 us to finish charging GLx and settle
T_SAMPLE_GUARD_S = 100e-9  # sample window must close this long before the LS gate starts to turn off

# Highest power-loop inductance expected for the stated layout (caps beside/under the FETs with
# many vias).  Rough budget: ~1-1.5 nH PCB + vias, ~1-1.5 nH for the HS+LS packages in series,
# plus margin.  2-3 nH is the expected value; 4 nH is the conservative upper bound used here.
L_LOOP_MAX_H = 1.5e-9

# DRV8304 IDRIVE settings (source A, sink A)
IDRIVE_SETTINGS: dict[str, tuple[float, float]] = {
    "15/30": (15e-3, 30e-3),
    "45/90": (45e-3, 90e-3),
    "60/120": (60e-3, 120e-3),
    "90/180": (90e-3, 180e-3),
    "105/210": (105e-3, 210e-3),
    "135/270": (135e-3, 270e-3),
    "150/300": (150e-3, 300e-3),
}
RECOMMENDED_IDRIVE = "15/30"

TSTEP_NS = 0.25
TSTEP_FINE_NS = 0.1  # convergence re-run

WR_VARS = (
    "v(sh_node)",
    "v_gs_ls",
    "v_gs_hs",
    "i_shunt",
    "v_sp_com",
    "v(sp_pin)",
    "v(sn_pin)",
    "v_ds_ls",
    "v_ds_hs",
    "v(so_pin)",
    "v(adc_pin)",
    "v(c_adc)",
)


# ---------------------------------------------------------------------------------------------
# Data types
# ---------------------------------------------------------------------------------------------
@dataclass(frozen=True)
class CurrentSenseCase:
    """Declarative specification of one current-sense simulation corner."""

    name: str
    plot_filename: str
    # operating point
    v_batt: float = 21.0
    i_phase: float = 16.0
    c_cer_phase_f: float = 11.8e-6  # derated at 21 V; 14 uF at 15 V
    # DRV8304
    g_csa: float = 10.0
    t_set_csa_s: float = 400e-9
    v_bias: float = 1.65
    i_drivep_a: float = 15e-3
    i_driven_a: float = 30e-3
    t_pd_drv_s: float = 180e-9
    t_dead_drv_s: float = 120e-9
    # MCU / ADC timing
    t_pwm_edge_s: float = 200e-9
    t_wait_s: float = T_WAIT_S
    t_latr_s: float = 83e-9
    t_s_adc_s: float = 69e-9
    v_cadc_init_v: float = 0.0
    # layout + FET model
    l_loop_h: float = L_LOOP_MAX_H
    tt_body_s: float = FET_TT_TYP_S
    c_gs_fet_f: float = 2.35e-9
    vto_fet_v: float = 1.8
    kp_fet: float = 80.0
    # expectations
    expect_pass: bool = True
    expect_tags: tuple[str, ...] = ()  # for expected-fail cases: tags that must be among the failures
    check_convergence: bool = False
    params: dict[str, str] = field(default_factory=dict)  # extra raw .param overrides

    @property
    def t_hs_off_s(self) -> float:
        return self.t_pwm_edge_s + self.t_pd_drv_s

    @property
    def t_ls_on_s(self) -> float:
        return self.t_hs_off_s + self.t_dead_drv_s

    @property
    def t_smp_start_s(self) -> float:
        return self.t_pwm_edge_s + self.t_wait_s + self.t_latr_s

    @property
    def t_smp_end_s(self) -> float:
        return self.t_smp_start_s + self.t_s_adc_s

    @property
    def v_ideal_v(self) -> float:
        # Shunt carries -I_PHASE while the LS conducts; DRV8304 output is inverting.
        return self.v_bias + self.g_csa * self.i_phase * R_SENSE

    def overrides(self) -> dict[str, str]:
        raw = {
            "V_BATT": self.v_batt,
            "I_PHASE": self.i_phase,
            "C_CER_PHASE": self.c_cer_phase_f,
            "G_CSA": self.g_csa,
            "T_SET_CSA": self.t_set_csa_s,
            "V_BIAS": self.v_bias,
            "I_DRIVEP": self.i_drivep_a,
            "I_DRIVEN": self.i_driven_a,
            "T_PD_DRV": self.t_pd_drv_s,
            "T_DEAD_DRV": self.t_dead_drv_s,
            "T_PWM_EDGE": self.t_pwm_edge_s,
            "T_WAIT": self.t_wait_s,
            "T_LATR": self.t_latr_s,
            "T_S_ADC": self.t_s_adc_s,
            "V_CADC_INIT": self.v_cadc_init_v,
            "L_LOOP": self.l_loop_h,
            "TT_BODY": self.tt_body_s,
            "C_GS_FET": self.c_gs_fet_f,
            "VTO_FET": self.vto_fet_v,
            "KP_FET": self.kp_fet,
        }
        out = {k: f"{v:.9g}" for k, v in raw.items()}
        out.update(self.params)
        return out

    def tran_spec(self, step_ns: float = TSTEP_NS) -> str:
        stop_ns = (self.t_smp_end_s + 300e-9) * 1e9
        return f"{step_ns:g}n {stop_ns:.0f}n"


@dataclass(frozen=True)
class CurrentSenseMetrics:
    """Metrics extracted from an ngspice current-sense transient run."""

    v_ideal_v: float
    v_cadc_v: float  # C_ADC just after the sampling switch opens
    cadc_err_mv: float
    cadc_err_lsb: float
    cadc_err_a: float
    hold_droop_lsb: float
    jitter_err_mv: float
    jitter_err_lsb: float
    v_sp_com_sample_v: float
    v_spsn_abs_peak_v: float  # max over both pins, absolute value
    t_spsn_over_cont_ns: float  # longest contiguous time any SPx/SNx pin exceeds the derated continuous limit
    v_gl_min_v: float
    v_gl_max_v: float
    t_gl_below_cont_ns: float
    v_sh_min_v: float
    v_sh_max_v: float
    t_sh_below_cont_ns: float
    v_gs_hs_min_v: float
    v_gs_hs_max_v: float
    v_gs_hs_at_ls_on_v: float
    v_ds_ls_max_v: float
    v_ds_hs_max_v: float
    v_so_min_v: float
    v_so_max_v: float
    v_adc_pin_min_v: float
    v_adc_pin_max_v: float
    i_shunt_peak_a: float
    i_shunt_final_a: float
    v_gs_ls_final_v: float


@dataclass(frozen=True)
class CurrentSenseWaveforms:
    """Time-series waveforms extracted from ngspice wrdata."""

    time_ns: list[float]
    v_sh: list[float]
    v_gl: list[float]
    v_gs_hs: list[float]
    i_shunt: list[float]
    v_sp_com: list[float]
    v_sp_pin: list[float]
    v_sn_pin: list[float]
    v_so: list[float]
    v_adc_pin: list[float]
    v_cadc: list[float]


@dataclass(frozen=True)
class CurrentSenseResult:
    """Combined metrics and waveforms for a current-sense simulation run."""

    metrics: CurrentSenseMetrics
    waveforms: CurrentSenseWaveforms


# ---------------------------------------------------------------------------------------------
# Waveform helpers
# ---------------------------------------------------------------------------------------------
def value_at(times: list[float], values: list[float], t: float) -> float:
    """Linear interpolation of a sampled waveform."""
    i = bisect.bisect_left(times, t)
    if i <= 0:
        return values[0]
    if i >= len(times):
        return values[-1]
    t0, t1 = times[i - 1], times[i]
    frac = (t - t0) / (t1 - t0) if t1 > t0 else 0.0
    return values[i - 1] + frac * (values[i] - values[i - 1])


def longest_excursion_s(times: list[float], values: list[float], outside) -> float:
    """Longest contiguous duration for which outside(value) is true."""
    longest = 0.0
    run = 0.0
    for i in range(1, len(times)):
        if outside(values[i]):
            run += times[i] - times[i - 1]
            longest = max(longest, run)
        else:
            run = 0.0
    return longest


def _check_overrides_exist(case: CurrentSenseCase) -> None:
    """ngspice_common silently ignores overrides whose `.param` line is missing; fail loudly instead."""
    text = BASE_CIR.read_text(encoding="utf-8")
    for key in case.overrides():
        if f".param {key}" not in text:
            raise RuntimeError(f"{BASE_CIR.name} has no '.param {key}' line to override")


# ---------------------------------------------------------------------------------------------
# Simulation + metrics
# ---------------------------------------------------------------------------------------------
def run_current_sense_case(case: CurrentSenseCase, step_ns: float = TSTEP_NS) -> CurrentSenseResult:
    """Execute ngspice for a CurrentSenseCase and compute accuracy/safety metrics."""
    _check_overrides_exist(case)
    cols = run_ngspice(
        BASE_CIR,
        wr_vars=WR_VARS,
        param_overrides=case.overrides(),
        tran_spec=case.tran_spec(step_ns),
    )
    times = cols["time"]
    v_sh = cols["v(sh_node)"]
    v_gl = cols["v_gs_ls"]  # GLx pin voltage with respect to PGND
    v_gs_hs = cols["v_gs_hs"]
    i_shunt = cols["i_shunt"]
    v_sp_com = cols["v_sp_com"]
    v_sp_pin = cols["v(sp_pin)"]
    v_sn_pin = cols["v(sn_pin)"]
    v_ds_ls = cols["v_ds_ls"]
    v_ds_hs = cols["v_ds_hs"]
    v_so = cols["v(so_pin)"]
    v_adc_pin = cols["v(adc_pin)"]
    v_cadc = cols["v(c_adc)"]

    v_ideal = case.v_ideal_v
    t_smp_start = case.t_smp_start_s
    t_smp_end = case.t_smp_end_s

    # Sampled value: C_ADC right after the sampling switch opens (not at the end of the simulation).
    v_cadc_sampled = value_at(times, v_cadc, t_smp_end + 10e-9)
    cadc_err_v = abs(v_cadc_sampled - v_ideal)
    hold_droop_lsb = abs(v_cadc[-1] - v_cadc_sampled) / LSB_V

    # Worst-case ADC pin error across +/-50 ns around the end of sampling
    jitter_err_v = max(
        (
            abs(v_adc_pin[i] - v_ideal)
            for i, t in enumerate(times)
            if (t_smp_end - 50e-9) <= t <= (t_smp_end + 50e-9)
        ),
        default=cadc_err_v,
    )
    v_sp_com_sample = max(
        (abs(v_sp_com[i]) for i, t in enumerate(times) if t_smp_start <= t <= t_smp_end),
        default=abs(v_sp_com[-1]),
    )

    # Per-pin SPx/SNx amplitude (abs-max applies to each pin, not their average)
    spsn_abs = [max(abs(a), abs(b)) for a, b in zip(v_sp_pin, v_sn_pin)]
    t_spsn_over_cont = longest_excursion_s(times, spsn_abs, lambda v: v > ABS_MAX_DERATE * SPSN_CONT_V)
    t_gl_below_cont = longest_excursion_s(times, v_gl, lambda v: v < ABS_MAX_DERATE * GLX_CONT_MIN_V)
    t_sh_below_cont = longest_excursion_s(times, v_sh, lambda v: v < ABS_MAX_DERATE * SHX_CONT_MIN_V)

    metrics = CurrentSenseMetrics(
        v_ideal_v=v_ideal,
        v_cadc_v=v_cadc_sampled,
        cadc_err_mv=cadc_err_v * 1e3,
        cadc_err_lsb=cadc_err_v / LSB_V,
        cadc_err_a=cadc_err_v / (case.g_csa * R_SENSE),
        hold_droop_lsb=hold_droop_lsb,
        jitter_err_mv=jitter_err_v * 1e3,
        jitter_err_lsb=jitter_err_v / LSB_V,
        v_sp_com_sample_v=v_sp_com_sample,
        v_spsn_abs_peak_v=max(spsn_abs),
        t_spsn_over_cont_ns=t_spsn_over_cont * 1e9,
        v_gl_min_v=min(v_gl),
        v_gl_max_v=max(v_gl),
        t_gl_below_cont_ns=t_gl_below_cont * 1e9,
        v_sh_min_v=min(v_sh),
        v_sh_max_v=max(v_sh),
        t_sh_below_cont_ns=t_sh_below_cont * 1e9,
        v_gs_hs_min_v=min(v_gs_hs),
        v_gs_hs_max_v=max(v_gs_hs),
        v_gs_hs_at_ls_on_v=value_at(times, v_gs_hs, next((t for t, g in zip(times, v_gl) if g > case.vto_fet_v), times[-1])),
        v_ds_ls_max_v=max(v_ds_ls),
        v_ds_hs_max_v=max(v_ds_hs),
        v_so_min_v=min(v_so),
        v_so_max_v=max(v_so),
        v_adc_pin_min_v=min(v_adc_pin),
        v_adc_pin_max_v=max(v_adc_pin),
        i_shunt_peak_a=max(abs(i) for i in i_shunt),
        i_shunt_final_a=i_shunt[-1],
        v_gs_ls_final_v=v_gl[-1],
    )
    waveforms = CurrentSenseWaveforms(
        time_ns=[t * 1e9 for t in times],
        v_sh=v_sh,
        v_gl=v_gl,
        v_gs_hs=v_gs_hs,
        i_shunt=i_shunt,
        v_sp_com=v_sp_com,
        v_sp_pin=v_sp_pin,
        v_sn_pin=v_sn_pin,
        v_so=v_so,
        v_adc_pin=v_adc_pin,
        v_cadc=v_cadc,
    )
    return CurrentSenseResult(metrics=metrics, waveforms=waveforms)


def min_ls_on_time_s(case: CurrentSenseCase) -> float:
    """Shortest commanded LS on-time for which this sample point is valid (trigger at mid LS on-time)."""
    return 2.0 * case.t_wait_s


def max_duty(case: CurrentSenseCase, f_pwm_hz: float = F_PWM_HZ) -> float:
    return 1.0 - min_ls_on_time_s(case) * f_pwm_hz


def evaluate_case(case: CurrentSenseCase, m: CurrentSenseMetrics) -> list[str]:
    """Return a list of '[TAG] reason' failures; empty list means PASS."""
    d = ABS_MAX_DERATE
    failures: list[str] = []

    # --- Signal chain ---------------------------------------------------------------------
    if not (V_LIN_MIN - 1e-6 <= m.v_ideal_v <= V_LIN_MAX + 1e-6):
        failures.append(
            f"[SATURATION] Ideal CSA output {m.v_ideal_v:.3f} V outside DRV8304 linear range "
            f"[{V_LIN_MIN:.2f}, {V_LIN_MAX:.2f}] V"
        )
    if m.cadc_err_mv > MAX_CADC_ERR_V * 1e3:
        failures.append(
            f"[SETTLE-ERR] Sampled C_ADC error {m.cadc_err_mv:.2f} mV ({m.cadc_err_lsb:.2f} LSB, "
            f"{m.cadc_err_a:.3f} A) > {MAX_CADC_ERR_V * 1e3:.2f} mV (2.0 LSB)"
        )
    if m.jitter_err_mv > MAX_JITTER_ERR_V * 1e3:
        failures.append(
            f"[SETTLE-WINDOW] +/-50ns window ADC_pin error {m.jitter_err_mv:.2f} mV "
            f"({m.jitter_err_lsb:.2f} LSB) > {MAX_JITTER_ERR_V * 1e3:.2f} mV (4.0 LSB)"
        )
    if m.hold_droop_lsb > MAX_HOLD_DROOP_LSB:
        failures.append(
            f"[HOLD-DROOP] C_ADC moved {m.hold_droop_lsb:.2f} LSB after sampling (switch leakage artifact)"
        )
    if m.v_sp_com_sample_v > V_SP_COM_LINEAR_MAX:
        failures.append(
            f"[CM-LINEAR] Sample-window |V_SP,COM| {m.v_sp_com_sample_v:.3f} V > {V_SP_COM_LINEAR_MAX:.2f} V"
        )
    if m.v_adc_pin_min_v < V_ADC_PIN_MIN:
        failures.append(
            f"[ADC-NEG-INJ] ADC pin undershoot {m.v_adc_pin_min_v:.3f} V < {V_ADC_PIN_MIN:.2f} V"
        )

    # --- DRV8304 abs-max ratings (amplitude limits, with derating) -----------------------------
    if m.v_spsn_abs_peak_v > d * SPSN_PULSED_V:
        failures.append(
            f"[ABS-SPSN] SPx/SNx peak {m.v_spsn_abs_peak_v:.3f} V > {d * SPSN_PULSED_V:.2f} V "
            f"(pulsed +/-{SPSN_PULSED_V:.0f} V rating x{d})"
        )
    if m.t_spsn_over_cont_ns > T_PULSE_S * 1e9:
        failures.append(
            f"[ABS-SPSN] SPx/SNx above {d * SPSN_CONT_V:.2f} V for {m.t_spsn_over_cont_ns:.0f} ns "
            f"(> {T_PULSE_S * 1e9:.0f} ns pulsed window)"
        )
    if m.v_gl_min_v < d * GLX_PULSED_MIN_V:
        failures.append(f"[ABS-GLX] GLx undershoot {m.v_gl_min_v:.2f} V < {d * GLX_PULSED_MIN_V:.2f} V")
    if m.t_gl_below_cont_ns > T_PULSE_S * 1e9:
        failures.append(
            f"[ABS-GLX] GLx below {d * GLX_CONT_MIN_V:.2f} V for {m.t_gl_below_cont_ns:.0f} ns (> 200 ns)"
        )
    if m.v_gl_max_v > d * GLX_MAX_V:
        failures.append(f"[ABS-GLX] GLx overshoot {m.v_gl_max_v:.2f} V > {d * GLX_MAX_V:.2f} V")
    if m.v_sh_min_v < d * SHX_PULSED_MIN_V:
        failures.append(f"[ABS-SHX] SHx undershoot {m.v_sh_min_v:.2f} V < {d * SHX_PULSED_MIN_V:.2f} V")
    if m.t_sh_below_cont_ns > T_PULSE_S * 1e9:
        failures.append(
            f"[ABS-SHX] SHx below {d * SHX_CONT_MIN_V:.2f} V for {m.t_sh_below_cont_ns:.0f} ns (> 200 ns)"
        )
    sh_max_limit = case.v_batt + d * SHX_MAX_OVER_VM_V
    if m.v_sh_max_v > sh_max_limit:
        failures.append(f"[ABS-SHX] SHx overshoot {m.v_sh_max_v:.2f} V > VM + {d * SHX_MAX_OVER_VM_V:.1f} V")
    if m.v_gs_hs_min_v < d * GHX_SHX_MIN_V or m.v_gs_hs_max_v > d * GHX_SHX_MAX_V:
        failures.append(
            f"[ABS-GHX] GHx-SHx range [{m.v_gs_hs_min_v:.2f}, {m.v_gs_hs_max_v:.2f}] V outside "
            f"[{d * GHX_SHX_MIN_V:.2f}, {d * GHX_SHX_MAX_V:.2f}] V"
        )
    if m.v_so_min_v < d * SOX_MIN_V or m.v_so_max_v > V_REF:
        failures.append(f"[ABS-SOX] SOx range [{m.v_so_min_v:.2f}, {m.v_so_max_v:.2f}] V outside [-0.3, VREF]")

    # --- Power stage ------------------------------------------------------------------------
    for label, v in (("LS", m.v_ds_ls_max_v), ("HS", m.v_ds_hs_max_v)):
        if v > d * FET_VDS_MAX_V:
            failures.append(f"[FET-VDS] {label} V_DS peak {v:.1f} V > {d * FET_VDS_MAX_V:.1f} V")
    if m.v_gs_hs_at_ls_on_v > FET_VTH_MIN_V:
        failures.append(
            f"[SHOOT-THRU] HS V_GS = {m.v_gs_hs_at_ls_on_v:.2f} V when LS turn-on starts "
            f"(> V_th,min {FET_VTH_MIN_V} V): dead time too short for this IDRIVEN/Ciss"
        )
    if m.v_gs_ls_final_v < 7.0:
        failures.append(f"[GATE-DRIVE] LS V_GS = {m.v_gs_ls_final_v:.2f} V < 7.0 V (not fully on)")

    # --- Firmware timing ---------------------------------------------------------------------
    t_after_edge_s = case.t_wait_s + case.t_latr_s + case.t_s_adc_s + T_SAMPLE_GUARD_S
    if t_after_edge_s > min_ls_on_time_s(case):
        failures.append(
            f"[TIMING] Sample window closes {t_after_edge_s * 1e9:.0f} ns after the edge but the minimum "
            f"LS on-time is only {min_ls_on_time_s(case) * 1e9:.0f} ns"
        )

    return failures


# ---------------------------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------------------------
def _with_idrive(code: str, **kw) -> dict:
    src, snk = IDRIVE_SETTINGS[code]
    return dict(i_drivep_a=src, i_driven_a=snk, **kw)


# Worst-case stacks.  Both use 2x typical Qrr (hot junction) and the stated maximum loop inductance.
#   SLOW: worst for settling -- latest edge (t_PD max) sampled at the earliest trigger latency
#         (3 ADC cycles), largest Ciss (3.2 nF max - Crss), highest V_th, lowest g_fs (48 S min).
#   FAST: worst for ringing/abs-max -- fastest edges (minimum V_th, typical-high g_fs, typical Ciss).
SLOW_STACK = dict(
    t_pd_drv_s=250e-9,
    t_latr_s=83e-9,
    c_gs_fet_f=3.1e-9,
    vto_fet_v=2.0,
    kp_fet=23.0,
    tt_body_s=FET_TT_HOT_S,
)
FAST_STACK = dict(
    t_pd_drv_s=180e-9,
    t_latr_s=83e-9,
    c_gs_fet_f=2.35e-9,
    vto_fet_v=1.2,
    kp_fet=92.0,
    tt_body_s=FET_TT_HOT_S,
)
# C_ADC precharge that is farthest from the ideal sample for each polarity (worst kickback)
KICKBACK = {+20.0: 0.0, -20.0: V_REF}


def worst_case_set(idrive: str, with_convergence: bool, v_batts=(21.0, 15.0)) -> list[CurrentSenseCase]:
    """Both stacks x both current polarities x full/low battery at one IDRIVE setting."""
    cases: list[CurrentSenseCase] = []
    for stack_name, stack in (("slow", SLOW_STACK), ("fast", FAST_STACK)):
        for i_ph in (+20.0, -20.0):
            for vb in v_batts:
                cases.append(
                    CurrentSenseCase(
                        name=f"Worst-case {stack_name} stack, {i_ph:+.0f}A, {vb:.0f}V, IDRIVE {idrive} mA",
                        plot_filename=f"cs_worst_{stack_name}_{'p' if i_ph > 0 else 'm'}20a_{vb:.0f}v_{idrive.replace('/', '_')}.png",
                        v_batt=vb,
                        i_phase=i_ph,
                        c_cer_phase_f=11.8e-6 if vb > 18 else 14.0e-6,
                        v_cadc_init_v=KICKBACK[i_ph],
                        check_convergence=with_convergence,
                        **_with_idrive(idrive),
                        **stack,
                    )
                )
    return cases


def _build_cases() -> tuple[CurrentSenseCase, ...]:
    cases: list[CurrentSenseCase] = []
    ref = _with_idrive(RECOMMENDED_IDRIVE)

    # --- Typical-parts cases at the maximum loop inductance ---------------------------------
    cases += [
        CurrentSenseCase("Nominal +16A (21V, G=10)", "cs_nominal_p16a_g10.png", i_phase=16.0, **ref),
        CurrentSenseCase("Nominal -16A (21V, G=10)", "cs_nominal_m16a_g10.png", i_phase=-16.0, **ref),
        CurrentSenseCase(
            "Zero current 0A, C_ADC kickback from 3.3V", "cs_0a_g10_kickback.png",
            i_phase=0.0, v_cadc_init_v=3.3, **ref,
        ),
        CurrentSenseCase(
            "Gain 5 V/V +20A (t_SET=260ns)", "cs_p20a_g5.png",
            i_phase=20.0, g_csa=5.0, t_set_csa_s=260e-9, **ref,
        ),
        CurrentSenseCase(
            "Longer sampling SMP=7.5 (208ns), +20A, kickback 3.3V", "cs_smp7p5_p20a_g10.png",
            i_phase=20.0, t_s_adc_s=208e-9, v_cadc_init_v=3.3, **ref,
        ),
    ]

    # --- Worst-case stacks at the recommended IDRIVE (with convergence re-run) ----------------
    cases += worst_case_set(RECOMMENDED_IDRIVE, with_convergence=True)

    # --- Other IDRIVE codes: documented as expected-fail in the fast stack at -20 A ------------
    for code in IDRIVE_SETTINGS:
        if code == RECOMMENDED_IDRIVE:
            continue
        cases.append(
            CurrentSenseCase(
                f"Expected fail: IDRIVE {code} mA, fast stack, -20A", f"cs_fail_idrive_{code.replace('/', '_')}.png",
                i_phase=-20.0, v_cadc_init_v=V_REF, expect_pass=False, expect_tags=("ABS-SPSN",),
                **_with_idrive(code), **FAST_STACK,
            )
        )

    # --- Expected-fail corners ---------------------------------------------------------------
    cases += [
        CurrentSenseCase(
            "Expected fail: G=20 V/V saturates at +20A bidir", "cs_fail_g20_sat.png",
            i_phase=20.0, g_csa=20.0, t_set_csa_s=700e-9,
            expect_pass=False, expect_tags=("SATURATION",), **ref,
        ),
        CurrentSenseCase(
            "Expected fail: G=40 V/V unsettled at +2A (t_SET=1550ns > 1us)", "cs_fail_g40_unsettled.png",
            i_phase=2.0, g_csa=40.0, t_set_csa_s=1550e-9,
            expect_pass=False, expect_tags=("SETTLE-ERR",), **ref,
        ),
        CurrentSenseCase(
            "Expected fail: sample 0.6us after the edge (+20A, slow stack)", "cs_fail_twait_0p6us.png",
            i_phase=20.0, t_wait_s=0.6e-6,
            expect_pass=False, expect_tags=("SETTLE-ERR",), **ref, **SLOW_STACK,
        ),
        CurrentSenseCase(
            "Expected fail: Loop inductance 8nH (ring has not decayed at the sample)", "cs_fail_lloop_8nh.png",
            i_phase=-20.0, v_cadc_init_v=V_REF, l_loop_h=8e-9,
            expect_pass=False, expect_tags=("SETTLE-WINDOW",), **ref, **FAST_STACK,
        ),
    ]
    return tuple(cases)


TEST_CASES: tuple[CurrentSenseCase, ...] = _build_cases()


# ---------------------------------------------------------------------------------------------
# Reporting / plotting
# ---------------------------------------------------------------------------------------------
def print_report(case: CurrentSenseCase, m: CurrentSenseMetrics, failures: list[str]) -> None:
    status = "PASS" if not failures else "FAIL"
    print(f"=== {case.name} [{status}] ===")
    print(
        f"  C_ADC sampled / ideal: {m.v_cadc_v:.4f} V / {m.v_ideal_v:.4f} V "
        f"(err {m.cadc_err_mv:.2f} mV = {m.cadc_err_lsb:.2f} LSB = {m.cadc_err_a:.3f} A; droop {m.hold_droop_lsb:.2f} LSB)"
    )
    print(f"  +/-50ns window err   : {m.jitter_err_mv:.2f} mV ({m.jitter_err_lsb:.2f} LSB)")
    print(
        f"  SPx/SNx |peak| {m.v_spsn_abs_peak_v:.2f} V ({m.t_spsn_over_cont_ns:.0f} ns > {ABS_MAX_DERATE * SPSN_CONT_V:.1f} V)"
        f"  GLx [{m.v_gl_min_v:.2f}, {m.v_gl_max_v:.2f}] V  SHx [{m.v_sh_min_v:.2f}, {m.v_sh_max_v:.2f}] V"
    )
    print(
        f"  V_DS LS/HS peak {m.v_ds_ls_max_v:.1f}/{m.v_ds_hs_max_v:.1f} V  HS V_GS at LS-on {m.v_gs_hs_at_ls_on_v:.2f} V"
        f"  I_shunt peak {m.i_shunt_peak_a:.1f} A  sample-window |V_SP,COM| {m.v_sp_com_sample_v:.3f} V"
    )
    for f in failures:
        print(f"  [!] {f}")


def save_waveform_plot(
    case: CurrentSenseCase, result: CurrentSenseResult, failures: list[str], out_path: Path
) -> None:
    """Render a 4-panel transient waveform PNG for the current-sense chain."""
    plt = get_pyplot()
    m = result.metrics
    w = result.waveforms
    status = "PASS" if not failures else "FAIL"
    d = ABS_MAX_DERATE

    fig, axes = plt.subplots(4, 1, figsize=(10, 10), sharex=True)
    fig.suptitle(f"{case.name} [{status}]", fontsize=12, fontweight="bold")

    ax = axes[0]
    ax.plot(w.time_ns, w.v_sh, label="SHx (switch node)", color="#4c72b0", linewidth=1.2)
    ax.plot(w.time_ns, w.v_gl, label="GLx pin", color="#ff7f0e", linewidth=1.1)
    ax.axhline(d * GLX_PULSED_MIN_V, color="#ff7f0e", linestyle="--", alpha=0.5, label="GLx/SHx pulsed min")
    ax.set_ylabel("Voltage [V]")
    ax.legend(loc="upper right", fontsize=8, ncol=3)
    ax.grid(True, alpha=0.3)

    ax = axes[1]
    ax.plot(w.time_ns, w.i_shunt, label=f"I_shunt (peak {m.i_shunt_peak_a:.1f} A)", color="#d62728", linewidth=1.2)
    ax.set_ylabel("Shunt current [A]")
    ax.legend(loc="upper right", fontsize=8)
    ax.grid(True, alpha=0.3)

    ax = axes[2]
    ax.plot(w.time_ns, w.v_sp_pin, label="SPx", color="#9467bd", linewidth=1.0)
    ax.plot(w.time_ns, w.v_sn_pin, label="SNx", color="#17becf", linewidth=1.0, alpha=0.8)
    for lim, style, lab in ((SPSN_PULSED_V * d, "--", "pulsed limit"), (SPSN_CONT_V * d, ":", "continuous limit")):
        ax.axhline(lim, color="#9467bd", linestyle=style, alpha=0.5, label=lab)
        ax.axhline(-lim, color="#9467bd", linestyle=style, alpha=0.5)
    ax.set_ylabel("SPx / SNx [V]")
    ax.legend(loc="upper right", fontsize=8, ncol=4)
    ax.grid(True, alpha=0.3)

    ax = axes[3]
    ax.plot(w.time_ns, w.v_so, label="SOx", color="#1f77b4", alpha=0.75)
    ax.plot(w.time_ns, w.v_adc_pin, label="ADC pin", color="#2ca02c", linewidth=1.2)
    ax.plot(w.time_ns, w.v_cadc, label=f"C_ADC (err {m.cadc_err_mv:.2f} mV)", color="#d62728", linewidth=1.5)
    ax.axhline(m.v_ideal_v, color="black", linestyle=":", alpha=0.7, label=f"Ideal ({m.v_ideal_v:.3f} V)")
    ax.set_ylabel("Sense / ADC [V]")
    ax.set_xlabel("Time [ns]")
    ax.legend(loc="right", fontsize=8, ncol=2)
    ax.grid(True, alpha=0.3)

    for a in axes:
        a.axvline(case.t_pwm_edge_s * 1e9, color="gray", linestyle="--", alpha=0.5)
        a.axvline(case.t_hs_off_s * 1e9, color="gray", linestyle=":", alpha=0.5)
        a.axvspan(case.t_smp_start_s * 1e9, case.t_smp_end_s * 1e9, color="#2ca02c", alpha=0.12)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"  Saved plot: {out_path}")


# ---------------------------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------------------------
def convergence_failures(case: CurrentSenseCase, coarse: CurrentSenseMetrics) -> list[str]:
    """Re-run at a finer timestep; ring peaks and sampled error must agree."""
    fine = run_current_sense_case(case, step_ns=TSTEP_FINE_NS).metrics
    out: list[str] = []
    pairs = (
        ("SPx/SNx peak [V]", coarse.v_spsn_abs_peak_v, fine.v_spsn_abs_peak_v, 0.05, 0.05),
        ("GLx min [V]", coarse.v_gl_min_v, fine.v_gl_min_v, 0.10, 0.25),
        ("SHx min [V]", coarse.v_sh_min_v, fine.v_sh_min_v, 0.10, 0.25),
        ("I_shunt peak [A]", coarse.i_shunt_peak_a, fine.i_shunt_peak_a, 0.05, 1.0),
    )
    for label, a, b, rel, absolute in pairs:
        if abs(a - b) > max(rel * max(abs(a), abs(b)), absolute):
            out.append(f"[NUMERICS] {label}: {a:.3f} at {TSTEP_NS:g} ns vs {b:.3f} at {TSTEP_FINE_NS:g} ns")
    for label, a, b in (
        ("sampled error [LSB]", coarse.cadc_err_lsb, fine.cadc_err_lsb),
        ("window error [LSB]", coarse.jitter_err_lsb, fine.jitter_err_lsb),
    ):
        if abs(a - b) > 0.25:
            out.append(f"[NUMERICS] {label}: {a:.2f} at {TSTEP_NS:g} ns vs {b:.2f} at {TSTEP_FINE_NS:g} ns")
    return out


def run_test_case(case: CurrentSenseCase, plot_dir: Path | None) -> bool:
    """Run a single case and return True if its outcome matches expectations."""
    res = run_current_sense_case(case)
    failures = evaluate_case(case, res.metrics)
    if case.check_convergence:
        failures += convergence_failures(case, res.metrics)
    print_report(case, res.metrics, failures)

    if plot_dir is not None:
        save_waveform_plot(case, res, failures, plot_dir / case.plot_filename)

    passed = not failures
    if case.expect_pass and not passed:
        print(f"\nERROR: '{case.name}' failed checks unexpectedly!", file=sys.stderr)
        return False
    if not case.expect_pass:
        if passed:
            print(f"\nERROR: '{case.name}' was expected to FAIL but passed!", file=sys.stderr)
            return False
        missing = [t for t in case.expect_tags if not any(f.startswith(f"[{t}]") for f in failures)]
        if missing:
            print(f"\nERROR: '{case.name}' failed, but not for the expected reason(s): {missing}", file=sys.stderr)
            return False
    return True


def main() -> int:
    ref = CurrentSenseCase("ref", "ref.png")
    return run_test_cli(
        description="Run ngspice current-sense shunt & STM32C5 ADC tests.",
        cases=TEST_CASES,
        run_case_fn=run_test_case,
        default_plot_dir=TESTS_DIR / "plots",
        success_msg=(
            "ALL CHECKS PASSED (valid corners passed, expected-fail corners failed for the expected reasons).\n"
            f"Firmware timing assumed: ADC trigger {T_WAIT_S * 1e6:.2f} us after the PWM edge "
            f"(mid LS on-time) -> minimum LS on-time {min_ls_on_time_s(ref) * 1e6:.2f} us, "
            f"max duty {max_duty(ref) * 100:.1f}% at {F_PWM_HZ / 1e3:.0f} kHz; "
            f"IDRIVE {RECOMMENDED_IDRIVE} mA; loop inductance assumed up to {L_LOOP_MAX_H * 1e9:.1f} nH."
        ),
    )


if __name__ == "__main__":
    sys.exit(main())
