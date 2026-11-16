* Common netlist for the soft-start circuit (5S LiPo -> IRF4905 -> 10cm 14AWG -> 600uF)
* Parameters expected from the including file:
*   V_BATT_MAX : Battery voltage (e.g. 21.0 for fully charged 5S LiPo)
*   C_GATE     : Gate soft-start capacitor C1 (nominal 470n)
*   R1_VAL     : Gate-source resistor R1 (nominal 100k)
*   R2_VAL     : Gate-ground pulldown resistor R2 (nominal 150k)
*   V_TH       : IRF4905 gate threshold voltage (datasheet -2.0 to -4.0, nominal -3.0)

* IRF4905 P-Channel Power MOSFET (TO-220AB)
* Matched to datasheet: Vds=-55V, Vto=-3.0V typ, Rds(on)~16-20mOhm, Ciss~3400pF, Crss~640pF, Coss~1400pF
.model IRF4905 VDMOS(pchan Vto={V_TH} Kp=9 Rd=8m Rs=5m Rg=2
+ Cgdmax=2.0n Cgdmin=200p Cgs=2.8n Cjo=1.4n Is=1e-11 Rb=5m)

* 10V Zener diode D1 (gate-source protection)
.model DZ10V D(Is=1e-12 Rs=5 Bv=10.0 Ibv=1e-3)

* J6: 5S LiPo battery plugged in at t = 10us (10us rise time), 20mOhm pack ESR
.param T_PLUG = 10u
V_batt batt_ideal 0 PULSE(0 {V_BATT_MAX} {T_PLUG} 10u 10u 1 2)
R_batt batt_ideal v_in 20m

* Gate soft-start & Zener clamp network (C1, R1, D1, SW1 closed, R2)
C1 v_in n_rc {C_GATE}
R1 v_in n_rc {R1_VAL}
D1 n_rc v_in DZ10V
R_sw n_rc gate 10m
R2 gate 0 {R2_VAL}

* Q1: High-side P-MOSFET (Drain=batt_sw, Gate=gate, Source=v_in, Bulk=v_in)
M1 batt_sw gate v_in v_in IRF4905

* 10cm of 14AWG stranded copper wire (~1 mOhm DC resistance, ~100 nH inductance)
R_wire batt_sw batt_mid 1m
L_wire batt_mid batt_bus 100n

* Downstream +BATT bulk capacitance: 600uF with 20mOhm ESR
R_esr batt_bus cap_int 20m
C_load cap_int 0 600u
