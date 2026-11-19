* Common netlist for low-side current-shunt sensing:
*   5S LiPo -> Pour + Bulk Caps -> Phase Ceramic Caps -> BSC059N04LSG Half-Bridge
*   -> 7 mOhm Kelvin Shunt (CRE2512-FZ-R007E-3) -> DRV8304 CSA -> Trace -> STM32C562CE SAR ADC
*
* Expected parameters from including file:
*   V_BATT        : Bus voltage (e.g. 21.0 V for 5S full, 15.0 V min)
*   I_PHASE       : Motor phase current in Amps (+ = into motor from SHx, - = from motor into SHx)
*   G_CSA         : DRV8304 CSA gain (5, 10, 20, 40 V/V)
*   T_SET_CSA     : DRV8304 CSA 1% settling time in s (260n, 400n, 700n, 1550n)
*   V_BIAS        : CSA zero-current output bias (1.65 for bidir, 3.0 for unidir)
*   I_DRIVEP      : Gate driver peak source current (A), e.g. 15m..150m
*   I_DRIVEN      : Gate driver peak sink current (A), e.g. 30m..300m
*   T_PD_DRV      : DRV8304 propagation delay (s), e.g. 180n typ, 250n max
*   T_DEAD_DRV    : DRV8304 programmed dead time (s), added AFTER the HS V_GS hand-shake, e.g. 120n
*   T_PWM_EDGE    : MCU PWM edge time (s), e.g. 200n
*   T_WAIT        : Wait time from MCU PWM edge to ADC trigger (s), e.g. 1.0u
*   T_LATR        : STM32C5 ADC trigger-to-sample latency (s), e.g. 83n (3 cycles @ 36 MHz)
*   T_S_ADC       : STM32C5 ADC sampling time (s), e.g. 69n (SMP=2.5), 208n (SMP=7.5)
*   V_CADC_INIT   : Residual voltage on C_ADC before sampling (0.0 or 3.3 V)
*   L_LOOP        : Tight phase power-loop parasitic inductance (H), e.g. 3n
*   L_SHUNT_KELVIN: Residual ESL inside 2512 Kelvin sense taps (H), e.g. 1.3n
*   L_SHUNT_PAD   : High-side pad/trace ESL between LS FET source and SP tap (H), e.g. 0.7n
*   L_SHUNT       : SN pad-to-PGND via/pour ESL outside Kelvin taps (H), e.g. 0.3n (total 2512 ESL ~2.3n)
*   C_CER_PHASE   : Effective 1/3rd phase ceramic capacitance (F), e.g. 11.8u (derated at 21V)
*   TT_BODY       : MOSFET body-diode transit time (s). Stored charge = TT_BODY * I_F, so
*                   Qrr ~= TT_BODY * I_F. BSC059N04LS G datasheet: Qrr = 23 nC typ
*                   (V_R=20V, di/dt=400A/us)  ->  1.15n gives 23 nC at 20 A.
*   C_GS_FET      : MOSFET gate-source capacitance (F). Datasheet Ciss = 2400 pF typ / 3200 pF max
*                   (V_DS=20V), Crss = 28 pF  ->  2.35n typ, 3.1n max.
*   VTO_FET       : MOSFET threshold voltage (V). Datasheet V_GS(th) = 1.2 V min / 2.0 V max.
*   KP_FET        : MOSFET transconductance parameter (A/V^2). Id = KP/2 * Vov^2.
*                   Datasheet g_fs = 96 S typ / 48 S min at 50 A -> KP = gfs^2/(2*Id) = 92 / 23.

.param V_REF = 3.3
.param R_SENSE = 7m
.param R_ADC_SW = 1000
.param C_ADC_VAL = 3p
.param C_IO_PAD = 5p
.param C_PCB_TRACE = 10p
.param R_SO_TRACE = 0.2
.param L_SO_TRACE = 15n

* ---------------------------------------------------------------------------
* 1. Power Bus: Battery -> Wide Pour -> Distant 2x 100uF Polymer -> Local 1/3rd Ceramics
* ---------------------------------------------------------------------------
V_batt batt_src 0 DC {V_BATT}
R_batt batt_src batt_pour 20m
L_batt batt_pour pour_mid 50n

* Distant 2x 100uF EEH-ZC1H101P hybrid polymer bank (~20mOhm ESR each -> 10mOhm parallel, ~5nH ESL)
R_poly pour_mid poly_esr 10m
L_poly poly_esr poly_esl 2.5n
C_poly poly_esl 0 200u

* Wide battery pour trace from polymer bank to phase local bus
R_pour pour_mid vm_bus 2m
L_pour vm_bus vm_phase 15n

* Local 1/3rd phase ceramic bank: 4x 4.7uF (UMK316BJ475KL-T) + 1x 1uF (GMC21X7R105M50NT)
* Modeled as combined effective derated capacitance with parallel ESR/ESL + tight loop inductance
R_cer vm_phase cer_esr 3m
L_cer cer_esr cer_esl 0.4n
C_cer cer_esl pgnd_bus {C_CER_PHASE}

* Tight power-loop inductance between local ceramics and half-bridge drains/sources
L_loop_p vm_phase vd_hs {L_LOOP}
R_loop_p vd_hs vd_hs_int 1m

* ---------------------------------------------------------------------------
* 2. Infineon BSC059N04LSGATMA1 Power MOSFETs (OptiMOS3_40V_Spice.lib _L0)
* ---------------------------------------------------------------------------
* Package inductances adjusted from generic 3n/1n/1n fixture defaults to
* realistic 5x6 mm VSONP-8 / TDSON-8 leadless package ESLs (Lg=1.0n, Ld=0.2n, Ls=0.3n).
.SUBCKT BSC059N04LS_VSONP8 drain gate source
Lg     gate  g1    1.0n
Ld     drain d1    0.2n
Ls     source s1   0.3n
Rs      s1    s2   473u
Rg     g1    g2    1.4
M1      d2    g2    s2    s2    DMOS    L=1u   W=1u
.MODEL DMOS NMOS ( KP=203.4  VTO=2.74  THETA=0  VMAX=1.5e5  ETA=0.015  LEVEL=3)
Rd     d1    d2    3.2m TC=7m
Dbd     s2    d2    Dbt
.MODEL     Dbt    D(BV=45   M=0.3  CJO=1.73n  VJ=0.9V)
Dbody   s2   21    DBODY
.MODEL DBODY  D(IS=17.2p  N=1.1  RS=0.22u  EG=1.12  TT=3n)
Rdiode  d1  21    0.81m TC=7m
.MODEL   sw    NMOS(VTO=0  KP=10   LEVEL=1)
Maux      g2   c    a    a   sw
Maux2     b    d    g2    g2   sw
Eaux      c    a    d2    g2   1
Eaux2     d    g2   d2    g2   -1
* 0.1 Ohm series damping in Cox/Dgd branches prevents zero-resistance capacitive
* switching discontinuities in ngspice when V(d2,g2) crosses 0 V.
Rcox      b    b_c  0.1
Cox       b_c  d2   0.47n
.MODEL     DGD    D(M=0.6   CJO=0.47n   VJ=0.5)
Rpar      b    d2   1Meg
Rdgd      a    a_d  0.1
Dgd       a_d  d2   DGD
Rpar2     d2   a    10Meg
Cgs     g2    s2    2.33n
.ENDS BSC059N04LS_VSONP8

X_hs vd_hs_int gh sh_node BSC059N04LS_VSONP8
X_ls sh_node gl ls_src BSC059N04LS_VSONP8

* Switch-node RC snubber to PGND (with parasitic ESL)
R_snub sh_node snub_r {R_SNUB}
L_snub snub_r snub_c {L_SNUB}
C_snub snub_c pgnd_bus {C_SNUB}

* Pad/trace ESL between LS FET source and 2512 shunt SP Kelvin tap
L_shunt_pad ls_src sp_kelvin {L_SHUNT_PAD}

* ---------------------------------------------------------------------------
* 3. Motor Phase Load (inductive current I_PHASE commutating at SHx)
* ---------------------------------------------------------------------------
* Large inductance keeps phase current constant over the ~2us window
L_motor sh_node motor_mid 100u ic={I_PHASE}
I_motor_dc motor_mid 0 DC {I_PHASE}
R_motor_damp sh_node motor_mid 1k

* ---------------------------------------------------------------------------
* 4. DRV8304 Smart Gate Drive (t_PD, VGS hand-shake + t_DEAD, IDRIVEP/N)
* ---------------------------------------------------------------------------
* Before T_PWM_EDGE: HS ON, LS OFF. At T_PWM_EDGE + T_PD_DRV: HS gate starts to turn OFF
* (sink limited to IDRIVEN). The DRV8304 then waits until the HS V_GS monitor reports the FET is
* off (VGS_HS_OFF, hand-shake: datasheet 7.3.1.4.2 "automatic dead-time insertion"), adds the
* programmed t_DEAD, and only then turns the LS ON (source limited to IDRIVEP). The datasheet does
* not publish the monitor threshold; VGS_HS_OFF = 1.0 V (below the FET's 1.2 V minimum V_th) is an
* assumption. The LS turn-on time is therefore an output of the simulation, not an input.
* TDRIVE/I_STRONG (300 mA opposite-gate pulldown during LS t_DRIVE) is modeled on B_hs_drv below.
.param T_HS_OFF = {T_PWM_EDGE + T_PD_DRV}
.param VGS_HS_OFF = 1.0
.param I_STRONG = 300m

* High-side gate drive (referenced to SHx): 10V -> 0V at T_HS_OFF;
* sinks I_DRIVEN during HS turn-off/dead-time, then I_STRONG (300 mA) once LS t_DRIVE starts.
V_gh_drv gh_ideal sh_node PULSE(10 0 {T_HS_OFF} 5n 5n 10u 20u)
B_hs_drv sh_node gh I={ min(max((v(gh_ideal, sh_node) - v(gh, sh_node)) / 10, -({I_DRIVEN} + ({I_STRONG} - {I_DRIVEN}) * v(hs_off_dly))), {I_DRIVEP}) }
R_hs_holdoff gh sh_node 150k

* Hand-shake: hs_off_raw = 1 once HS is commanded off (gh_ideal < 5V) AND HS V_GS < VGS_HS_OFF (smooth step),
* then delayed by T_DEAD_DRV with a lossless line (Z0 = 1 ohm, matched at both ends, so the far end sees
* half amplitude -> gain of 2 below). Gating on gh_ideal breaks the DC feedback loop during .op.
B_hs_off hs_off_raw 0 V={ 0.5 * (1 - tanh((v(gh_ideal, sh_node) - 5.0) / 0.1)) * 0.5 * (1 - tanh((v(gh, sh_node) - {VGS_HS_OFF}) / 0.02)) }
R_dead_src hs_off_raw hs_off_in 1
T_dead hs_off_in 0 hs_off_dly 0 Z0=1 TD={T_DEAD_DRV}
R_dead_load hs_off_dly 0 1

* Low-side gate drive (referenced to PGND): 0V -> 10V when the hand-shake + t_DEAD has elapsed
B_gl_ideal gl_ideal pgnd_bus V={ 20 * v(hs_off_dly) }
B_ls_drv pgnd_bus gl I={ min(max((v(gl_ideal, pgnd_bus) - v(gl, pgnd_bus)) / 10, -{I_DRIVEN}), {I_DRIVEP}) }
R_ls_holdoff gl pgnd_bus 150k

* ---------------------------------------------------------------------------
* 5. 7 mOhm Kelvin Shunt (CRE2512-FZ-R007E-3, ~2.3 nH total 2512 ESL) & Diff Pair
* ---------------------------------------------------------------------------
* 2512 package ESL (~2.3 nH typ): L_SHUNT_PAD (0.7n) + L_SHUNT_KELVIN (1.3n) + L_SHUNT (0.3n).
R_shunt sp_kelvin shunt_mid {R_SENSE}
L_shunt_kelvin shunt_mid sn_kelvin {L_SHUNT_KELVIN}
L_shunt sn_kelvin pgnd_bus {L_SHUNT}

* Kelvin sense taps across R_shunt (short differential pair traces to DRV8304 SPx/SNx)
R_sp_tr sp_kelvin sp_drv 50m
L_sp_tr sp_drv sp_pin 3n
R_sn_tr sn_kelvin sn_drv 50m
L_sn_tr sn_drv sn_pin 3n
C_diff_tr sp_pin sn_pin 2p

* PGND star reference
R_pgnd_ref pgnd_bus 0 1m

* ---------------------------------------------------------------------------
* 6. DRV8304 Current-Shunt Amplifier (CSA) Behavioral Model
* ---------------------------------------------------------------------------
* Input impedance & common-mode clamp monitor
R_csa_in sp_pin sn_pin 100k
C_csa_in sp_pin sn_pin 5p

* Ideal transfer: V_target = V_BIAS - G_CSA * (V(sp_pin) - V(sn_pin))
* Dominant pole tau = T_SET_CSA / ln(100) = T_SET_CSA / 4.60517
.param TAU_CSA = {T_SET_CSA / 4.60517}

B_csa_ideal csa_raw 0 V={ {V_BIAS} - {G_CSA} * (v(sp_pin) - v(sn_pin)) }
R_csa_bw csa_raw csa_filt 1k
C_csa_bw csa_filt 0 {TAU_CSA / 1000}

* Output linear range clamp [0.25 V, V_REF - 0.25 V] + output stage impedance (30 Ohm)
.param V_LIN_MIN = 0.25
.param V_LIN_MAX = {V_REF - 0.25}
B_csa_clamp csa_out_ideal 0 V={ min(max(v(csa_filt), {V_LIN_MIN}), {V_LIN_MAX}) }
R_csa_out csa_out_ideal so_pin 30

* ---------------------------------------------------------------------------
* 7. SOx PCB Trace to STM32C562CE ADC Pin
* ---------------------------------------------------------------------------
R_so_tr so_pin so_mid {R_SO_TRACE}
L_so_tr so_mid adc_pin {L_SO_TRACE}
C_adc_pin adc_pin 0 {C_IO_PAD + C_PCB_TRACE}

* ---------------------------------------------------------------------------
* 8. STM32C562CE SAR ADC Sampling Switch & C_ADC (3 pF)
* ---------------------------------------------------------------------------
.param T_SMP_START = {T_PWM_EDGE + T_WAIT + T_LATR}
.param T_SMP_END   = {T_SMP_START + T_S_ADC}

.model ADC_SW SW(Ron={R_ADC_SW} Roff=1e13 Vt=0.5 Vh=0.05)
.model PRE_SW SW(Ron=100 Roff=1e13 Vt=0.5 Vh=0.05)

* Precharge C_ADC to V_CADC_INIT until 20ns before sampling starts (avoids needing uic)
V_pre_ctrl pre_ctrl 0 PULSE(1 0 {T_SMP_START - 20n} 1n 1n 10u 20u)
V_cadc_src cadc_init_v 0 DC {V_CADC_INIT}
S_pre cadc_init_v c_adc pre_ctrl 0 PRE_SW

* Sampling switch S1 closes during [T_SMP_START, T_SMP_END]
V_smp_ctrl smp_ctrl 0 PULSE(0 1 {T_SMP_START} 1n 1n {T_S_ADC} 20u)
S_adc adc_pin c_adc smp_ctrl 0 ADC_SW
C_adc c_adc 0 {C_ADC_VAL}
