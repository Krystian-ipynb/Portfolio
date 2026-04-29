%% ============================================================
%  66/11 kV Receiving Substation — Design Calculations
%  Author  : Krystian Rozanski
%  Project : 66/11 kV Single-Busbar Distribution Substation
%  Location: Brisbane, Queensland, Australia
%  Standard: AS/NZS 2067, IEC 60909, AS/NZS 4777.2, Energex NCR
%  Date    : 2025
%  Revision: Rev D — Vector group corrected to Dyn11; PLCC equipment
%             (coupling capacitors + LMUs) added; equipment summary
%             expanded with bus-section CBs, solar incomer, disconnector
%             breakdown; AS/NZS 4777.2 and IEC 60358 added to standards
% ============================================================
%
%  CONTENTS
%   Part 1  – Load Analysis & 20-Year Growth Projection
%   Part 2  – Transformer Sizing (N-1 Contingency)
%   Part 3  – System Parameters & Fault Level (IEC 60909)
%   Part 4  – Per-Unit Fault Analysis (Both TXs in Parallel)
%   Part 5  – Load Current Calculations & CT Ratio Selection
%   Part 6  – Voltage Regulation (OLTC Verification)
%   Part 7  – Circuit Breaker Rating Verification
%   Part 8  – Busbar Thermal Sizing (AS/NZS 2067)
%   Part 9  – 11 kV Feeder Sizing (4 Feeders)
%   Part 10 – Protection Settings Schedule
%   Part 11 – Neutral Earthing Resistor (NER) Sizing
%   Part 12 – PLCC Equipment (Coupling Capacitors & LMUs)
%   Part 13 – Final Summary & Equipment Schedule
% ============================================================

clear; clc; close all;

fprintf('============================================================\n');
fprintf('  66/11 kV RECEIVING SUBSTATION — DESIGN CALCULATIONS\n');
fprintf('  Brisbane, Queensland | AS/NZS 2067 | Energex NCR\n');
fprintf('============================================================\n\n');

%% ============================================================
%  PART 1: LOAD ANALYSIS & 20-YEAR GROWTH PROJECTION
% ============================================================

fprintf('--- PART 1: Load Analysis & Growth Projection ---\n\n');

pf        = 0.92;           % Power factor (lagging)
P_peak    = 28e6;           % Present peak real power (W)
P_avg     = 0.70 * P_peak;  % Average real power (70% load factor)
growth    = 0.03;           % Annual load growth rate (3%)
horizon   = 20;             % Design horizon (years) — per Energex NCR

S_peak_now = P_peak / pf;
S_avg_now  = P_avg  / pf;

fprintf('Present Peak Real Power       : %.1f MW\n',  P_peak/1e6);
fprintf('Present Average Real Power    : %.1f MW\n',  P_avg/1e6);
fprintf('Present Peak Apparent Power   : %.2f MVA\n', S_peak_now/1e6);
fprintf('Present Average Apparent Power: %.2f MVA\n\n', S_avg_now/1e6);

years          = 0:horizon;
P_peak_growth  = P_peak .* (1 + growth).^years;
S_peak_growth  = P_peak_growth ./ pf;
P_avg_growth   = P_avg  .* (1 + growth).^years;
S_avg_growth   = P_avg_growth  ./ pf;

S_design = max(S_peak_growth);  % 20-year peak apparent power

fprintf('20-Year Peak Real Power       : %.2f MW\n',  max(P_peak_growth)/1e6);
fprintf('20-Year Peak Apparent Power   : %.2f MVA\n\n', S_design/1e6);

% --- Load Growth Plot ---
figure('Name','Load Growth Projections — 20 Year Horizon','NumberTitle','off');
subplot(2,2,1);
plot(years, P_peak_growth/1e6, 'b-', 'LineWidth', 1.8);
title('Peak Real Power'); xlabel('Years'); ylabel('MW'); grid on;
xline(10,'--r','Label','10yr','LabelVerticalAlignment','bottom');
xline(20,'--k','Label','20yr','LabelVerticalAlignment','bottom');

subplot(2,2,2);
plot(years, S_peak_growth/1e6, 'r-', 'LineWidth', 1.8);
title('Peak Apparent Power'); xlabel('Years'); ylabel('MVA'); grid on;
xline(10,'--r'); xline(20,'--k');

subplot(2,2,3);
plot(years, P_avg_growth/1e6, 'b--', 'LineWidth', 1.8);
title('Average Real Power'); xlabel('Years'); ylabel('MW'); grid on;

subplot(2,2,4);
plot(years, S_avg_growth/1e6, 'r--', 'LineWidth', 1.8);
title('Average Apparent Power'); xlabel('Years'); ylabel('MVA'); grid on;

sgtitle('Load Growth Projections — 66/11 kV Substation, Brisbane QLD');

%% ============================================================
%  PART 2: TRANSFORMER SIZING — N-1 CONTINGENCY
%  Requirement: Each transformer must supply 100% peak load
%  independently (Energex NCR, AS/NZS 2067 Cl. 5.3)
% ============================================================

fprintf('--- PART 2: Transformer Sizing (N-1 Contingency) ---\n\n');

% Engineering margin per AS/NZS 2067 and Energex NCR
margin     = 1.20;
S_required = margin * S_design;

% Standard IEC transformer ratings (MVA): 20, 25, 31.5, 40, 50, 63
std_ratings = [20 25 31.5 40 50 63 80 100] * 1e6;
TX_rating   = std_ratings(find(std_ratings >= S_required, 1, 'first'));

fprintf('20-Year Design Load           : %.2f MVA\n', S_design/1e6);
fprintf('Required Rating (20%% margin)  : %.2f MVA\n', S_required/1e6);
fprintf('Selected Transformer Rating   : %.1f MVA (standard IEC)\n', TX_rating/1e6);
fprintf('Configuration                 : 2 x %.1f MVA, Dyn11, OLTC\n', TX_rating/1e6);
fprintf('N-1 Compliance                : CONFIRMED — each unit carries 100%% load\n\n');

%% ============================================================
%  PART 3: SYSTEM PARAMETERS & FAULT LEVEL (IEC 60909)
% ============================================================

fprintf('--- PART 3: System Parameters & Fault Level ---\n\n');

V_HV    = 66e3;     % HV nominal voltage (V)
V_LV    = 11e3;     % LV nominal voltage (V)
V_max   = 72.5e3;   % Maximum equipment voltage per IEC 60694 (V)
f       = 50;       % Frequency (Hz)
Z_pct   = 0.10;     % Transformer impedance (10%) — limits 11 kV fault level

% System source impedance — typical Energex 66 kV network
% (confirm with Energex connection study for actual value)
Ssc_66kV_system = 2500e6;  % Assumed 66 kV system fault level (MVA)
Z_source_pu = 1 / (Ssc_66kV_system / TX_rating);  % On transformer base

% Transformer impedance (pu on own base)
Z_tx_pu = Z_pct;

% Total impedance (single transformer in service — N-1 condition)
Z_total_N1 = Z_source_pu + Z_tx_pu;

% Total impedance (both transformers in parallel — worst case fault)
Z_total_parallel = Z_source_pu + Z_tx_pu / 2;

% 11 kV fault level — single transformer
Ssc_LV_N1       = TX_rating / Z_total_N1;
Isc_LV_N1       = Ssc_LV_N1 / (sqrt(3) * V_LV);

% 11 kV fault level — both transformers in parallel (worst case)
Ssc_LV_parallel = TX_rating / Z_total_parallel;
Isc_LV_parallel = Ssc_LV_parallel / (sqrt(3) * V_LV);

% 66 kV equivalent fault current
Isc_HV = Ssc_LV_N1 / (sqrt(3) * V_HV);

fprintf('Transformer Rating            : %.1f MVA\n',  TX_rating/1e6);
fprintf('Transformer Impedance (Z%%)    : %.0f%%\n',   Z_pct*100);
fprintf('System Fault Level (66 kV)    : %.0f MVA (assumed — verify Energex)\n', Ssc_66kV_system/1e6);
fprintf('\n  --- 11 kV Fault Level ---\n');
fprintf('  Single TX in service (N-1)  : %.1f MVA  |  %.2f kA\n', Ssc_LV_N1/1e6, Isc_LV_N1/1e3);
fprintf('  Both TXs in parallel (worst): %.1f MVA  |  %.2f kA\n', Ssc_LV_parallel/1e6, Isc_LV_parallel/1e3);
fprintf('  Maximum permitted (Energex) : 25 kA\n');

if Isc_LV_parallel <= 25e3
    fprintf('  Fault Level Check           : PASS (both TXs in parallel)\n\n');
else
    fprintf('  Fault Level Check           : FAIL — increase Z%% or add current limiting reactor\n\n');
end

fprintf('Equivalent 66 kV Fault Current: %.2f kA\n\n', Isc_HV/1e3);

t_fault = 3;  % Fault duration (s) per AS/NZS 2067

%% ============================================================
%  PART 4: PER-UNIT FAULT ANALYSIS
% ============================================================

fprintf('--- PART 4: Per-Unit Fault Analysis ---\n\n');

S_base  = TX_rating;     % MVA base
V_base_HV = V_HV;        % V
V_base_LV = V_LV;        % V

Z_base_HV = V_base_HV^2 / S_base;
Z_base_LV = V_base_LV^2 / S_base;

% Source impedance on common base
Z_src_pu   = S_base / Ssc_66kV_system;

% Transformer impedance (pu)
Z_tx1_pu   = Z_pct;
Z_tx2_pu   = Z_pct;

% Case 1: TX1 only (N-1, TX2 out of service)
Z_fault_case1 = Z_src_pu + Z_tx1_pu;
I_fault_pu_1  = 1 / Z_fault_case1;
I_fault_A_1   = I_fault_pu_1 * (S_base / (sqrt(3) * V_base_LV));

% Case 2: Both TXs in parallel (worst case for 11 kV busbar fault)
Z_tx_parallel_pu = Z_tx1_pu * Z_tx2_pu / (Z_tx1_pu + Z_tx2_pu);
Z_fault_case2    = Z_src_pu + Z_tx_parallel_pu;
I_fault_pu_2     = 1 / Z_fault_case2;
I_fault_A_2      = I_fault_pu_2 * (S_base / (sqrt(3) * V_base_LV));

fprintf('  S_base                      : %.1f MVA\n', S_base/1e6);
fprintf('  Z_source (pu)               : %.4f pu\n',  Z_src_pu);
fprintf('  Z_transformer (pu)          : %.4f pu\n',  Z_tx1_pu);
fprintf('\n  Case 1 — TX1 Only (N-1):\n');
fprintf('    Z_total (pu)              : %.4f pu\n',  Z_fault_case1);
fprintf('    I_fault (11 kV)           : %.2f kA\n',  I_fault_A_1/1e3);
fprintf('\n  Case 2 — Both TXs in Parallel (Worst Case):\n');
fprintf('    Z_total (pu)              : %.4f pu\n',  Z_fault_case2);
fprintf('    I_fault (11 kV)           : %.2f kA\n',  I_fault_A_2/1e3);
fprintf('    Limit (Energex / AS 2067) : 25.00 kA\n\n');

Isc_LV_design = max(I_fault_A_1, I_fault_A_2);  % Use worst case for equipment selection

%% ============================================================
%  PART 5: LOAD CURRENT CALCULATIONS & CT RATIO SELECTION
% ============================================================

fprintf('--- PART 5: Load Currents & CT Ratio Selection ---\n\n');

I_HV_rated = TX_rating / (sqrt(3) * V_HV);
I_LV_rated = TX_rating / (sqrt(3) * V_LV);

% Standard CT ratios (IEC 60044-1)
CT_HV_std = [200 300 400 600];
CT_LV_std = [1000 1200 1500 2000 2500 3000];

CT_HV_selected = CT_HV_std(find(CT_HV_std >= I_HV_rated * 1.25, 1, 'first'));
CT_LV_selected = CT_LV_std(find(CT_LV_std >= I_LV_rated * 1.25, 1, 'first'));

fprintf('HV Rated Current (%.0f MVA TX)  : %.1f A\n',  TX_rating/1e6, I_HV_rated);
fprintf('LV Rated Current (%.0f MVA TX)  : %.1f A\n',  TX_rating/1e6, I_LV_rated);
fprintf('\n  CT Ratio Selection (IEC 60044-1, 1.25x margin):\n');
fprintf('    66 kV CT Ratio            : %d/1 A  (metering class 0.2S, protection 5P20)\n', CT_HV_selected);
fprintf('    11 kV CT Ratio            : %d/1 A  (metering class 0.5, protection 5P20)\n\n', CT_LV_selected);

% 11 kV feeder current (4 feeders, assumed equal loading with 0.85 diversity)
n_feeders       = 4;
diversity       = 0.85;
I_feeder_each   = (I_LV_rated * diversity) / n_feeders;
CT_feeder_std   = [200 300 400 600 800];
CT_feeder_sel   = CT_feeder_std(find(CT_feeder_std >= I_feeder_each * 1.25, 1, 'first'));

fprintf('  11 kV Feeder Current (each) : %.1f A  (%.0f%% diversity, %d feeders)\n', ...
        I_feeder_each, diversity*100, n_feeders);
fprintf('  Feeder CT Ratio             : %d/1 A\n\n', CT_feeder_sel);

%% ============================================================
%  PART 6: VOLTAGE REGULATION & OLTC VERIFICATION
%  Formula per AS/NZS 2067 and IEC 60076-1
% ============================================================

fprintf('--- PART 6: Voltage Regulation & OLTC ---\n\n');

R_pct  = 0.50;   % Transformer resistance (% of rated) — typical 40 MVA unit
X_pct  = sqrt(Z_pct^2 - (R_pct/100)^2) * 100;  % Reactance %

% Voltage regulation at full load, 0.92 pf lagging
phi    = acos(pf);
dV_pct = R_pct * pf + X_pct * sin(phi);

fprintf('Transformer R%%                 : %.2f%%\n',  R_pct);
fprintf('Transformer X%%                 : %.2f%%\n',  X_pct);
fprintf('Voltage Regulation at FL       : %.2f%%\n',  dV_pct);

if dV_pct <= 5.0
    fprintf('Regulation Check               : PASS (limit = 5%%)\n');
else
    fprintf('Regulation Check               : FAIL — OLTC range must compensate\n');
end

% OLTC tap range — standard ±10% in 1.25% steps (Energex typical)
OLTC_range  = 0.10;   % ±10%
OLTC_step   = 0.0125; % 1.25% per step
n_taps      = OLTC_range / OLTC_step;

fprintf('\n  OLTC Specification:\n');
fprintf('    Range                     : ±%.0f%% (±%.3f pu)\n',  OLTC_range*100, OLTC_range);
fprintf('    Step Size                 : %.2f%%\n',  OLTC_step*100);
fprintf('    Number of Tap Steps       : ±%.0f steps\n\n', n_taps);

%% ============================================================
%  PART 7: CIRCUIT BREAKER RATING VERIFICATION
%  Per IEC 62271-100 and AS/NZS 2067
% ============================================================

fprintf('--- PART 7: Circuit Breaker Verification ---\n\n');

% 66 kV circuit breakers
CB_HV_rated_kA = 31.5;  % kA — standard IEC rating
CB_HV_rated_kV = 72.5;  % kV — matches V_max

% 11 kV circuit breakers
CB_LV_rated_kA = 31.5;  % kA
CB_LV_rated_kV = 12.0;  % kV

fprintf('  66 kV Circuit Breaker:\n');
fprintf('    Rated Breaking Capacity   : %.1f kA\n', CB_HV_rated_kA);
fprintf('    Maximum Voltage Rating    : %.1f kV\n', CB_HV_rated_kV);
fprintf('    Required Breaking (Isc)   : %.2f kA\n', Isc_HV/1e3);
if CB_HV_rated_kA * 1e3 >= Isc_HV
    fprintf('    66 kV CB Check            : PASS\n\n');
else
    fprintf('    66 kV CB Check            : FAIL\n\n');
end

fprintf('  11 kV Circuit Breaker:\n');
fprintf('    Rated Breaking Capacity   : %.1f kA\n', CB_LV_rated_kA);
fprintf('    Maximum Voltage Rating    : %.1f kV\n', CB_LV_rated_kV);
fprintf('    Required Breaking (Isc)   : %.2f kA\n', Isc_LV_design/1e3);
if CB_LV_rated_kA * 1e3 >= Isc_LV_design
    fprintf('    11 kV CB Check            : PASS\n\n');
else
    fprintf('    11 kV CB Check            : FAIL\n\n');
end

%% ============================================================
%  PART 8: BUSBAR THERMAL SIZING
%  Per AS/NZS 2067 and IEC 60865-1
% ============================================================

fprintf('--- PART 8: Busbar Thermal Sizing ---\n\n');

% Thermal sizing — fault withstand (IEC 60865-1)
k_Al    = 143;   % Aluminium thermal constant
A_fault = (Isc_LV_design * sqrt(t_fault)) / k_Al;

% Continuous current sizing
J_Al    = 1.5;   % A/mm² — aluminium, outdoor tubular busbar
A_cont  = I_LV_rated / J_Al;

A_busbar = max(A_fault, A_cont);

% Round up to nearest standard tubular busbar (mm²)
std_bus = [120 150 185 240 300 400 500 630];
A_selected = std_bus(find(std_bus >= A_busbar, 1, 'first'));

fprintf('  Fault Withstand Sizing:\n');
fprintf('    Fault Current (worst)     : %.2f kA\n', Isc_LV_design/1e3);
fprintf('    Fault Duration            : %.1f s\n',  t_fault);
fprintf('    Min. Area (fault)         : %.1f mm²\n', A_fault);
fprintf('\n  Continuous Current Sizing:\n');
fprintf('    LV Rated Current          : %.1f A\n',  I_LV_rated);
fprintf('    Current Density (Al)      : %.1f A/mm²\n', J_Al);
fprintf('    Min. Area (continuous)    : %.1f mm²\n', A_cont);
fprintf('\n  Governing Area             : %.1f mm²\n', A_busbar);
fprintf('  Selected Standard Size      : %d mm²  aluminium tubular busbar\n\n', A_selected);

%% ============================================================
%  PART 9: 11 kV FEEDER SIZING (4 FEEDERS)
% ============================================================

fprintf('--- PART 9: 11 kV Feeder Sizing (4 Feeders) ---\n\n');

% Each feeder carries equal share with diversity factor
I_each_feeder = I_feeder_each;

% XLPE cable — 11 kV, aluminium conductor
% Standard ratings (approximate, 90°C XLPE, flat formation, 0.8 derate for Brisbane ambient)
cable_sizes_mm2  = [95  120  150  185  240  300];
cable_ratings_A  = [200 235  270  310  360  405];  % Derated for QLD 35°C ground temp

sel_idx   = find(cable_ratings_A >= I_each_feeder * 1.25, 1, 'first');
cable_sel = cable_sizes_mm2(sel_idx);
cable_Ir  = cable_ratings_A(sel_idx);

fprintf('  Feeder Current (each)       : %.1f A\n',   I_each_feeder);
fprintf('  Required Rating (1.25x)     : %.1f A\n',   I_each_feeder * 1.25);
fprintf('  Selected Cable              : %d mm² Al XLPE 11 kV\n', cable_sel);
fprintf('  Cable Continuous Rating     : %d A (derated, Brisbane ambient)\n', cable_Ir);
fprintf('  Feeder CB Rating            : 630 A, 31.5 kA (IEC 62271-100)\n\n');

%% ============================================================
%  PART 10: PROTECTION SETTINGS SCHEDULE
%  Per Energex Protection Guidelines and IEC 60255
% ============================================================

fprintf('--- PART 10: Protection Settings Schedule ---\n\n');

% --- 66 kV Line Protection ---
fprintf('  66 kV Incoming Line Protection (per bay):\n');
fprintf('    Function 51  Overcurrent pickup   : %.0f A  (1.2x I_HV rated)\n', I_HV_rated * 1.2);
fprintf('    Function 51  Time multiplier      : 0.10 s  (IEC standard inverse)\n');
fprintf('    Function 51N Earth fault pickup   : %.0f A  (20%% of CT primary)\n', CT_HV_selected * 0.20);
fprintf('    Function 27  Undervoltage         : 90%% of nominal (59.4 kV)\n');
fprintf('    Function 59  Overvoltage          : 110%% of nominal (72.6 kV)\n\n');

% --- Transformer Differential Protection ---
fprintf('  Transformer Differential Protection (87T):\n');
fprintf('    Operate current (Is1)     : 0.20 pu  (20%% of rated current)\n');
fprintf('    Bias slope 1 (k1)         : 25%%  (through-fault stability)\n');
fprintf('    Bias slope 2 (k2)         : 50%%  (CT saturation region)\n');
fprintf('    Inrush restraint          : 2nd harmonic blocking, 15%% threshold\n\n');

% --- 11 kV Incomer Protection ---
fprintf('  11 kV Incomer Protection (per transformer secondary):\n');
fprintf('    Function 51  Overcurrent pickup   : %.0f A  (1.25x I_LV rated)\n', round(I_LV_rated * 1.25 / 10) * 10);
fprintf('    Function 51  Time multiplier      : 0.40 s  (graded above feeders)\n');
fprintf('    Function 51N Earth fault pickup   : %.0f A  (10%% of CT primary)\n', CT_LV_selected * 0.10);
fprintf('    Function 50  Inst. overcurrent    : %.0f A  (8x I_LV rated)\n', round(I_LV_rated * 8 / 100) * 100);
fprintf('    Function 87T Differential         : see above\n\n');

% --- 11 kV Feeder Protection ---
fprintf('  11 kV Feeder Protection (each of 4 feeders):\n');
fprintf('    Function 51  Overcurrent pickup   : %.0f A  (1.25x feeder rated)\n', round(I_each_feeder * 1.25 / 10) * 10);
fprintf('    Function 51  Time multiplier      : 0.10 s  (fastest in grading chain)\n');
fprintf('    Function 51N Earth fault pickup   : %.0f A  (10%% CT primary)\n', round(CT_feeder_sel * 0.10));
fprintf('    Function 79  Auto-reclose         : 1 shot, dead time 0.5 s (overhead lines)\n\n');

%% ============================================================
%  PART 11: NEUTRAL EARTHING RESISTOR (NER) SIZING
%  Per AS/NZS 2067 Cl. 8 and Energex NCR
%  Transformer vector group is Dyn11 — the LV winding is star
%  with neutral brought out. NER connects directly to this
%  neutral point. No separate earthing transformer required.
%  High-resistance earthing limits earth fault current to
%  reduce step/touch voltages and arc flash energy.
% ============================================================

fprintf('--- PART 11: Neutral Earthing Resistor (NER) Sizing ---\n\n');
fprintf('  NOTE: Dyn11 vector group — NER connects directly to\n');
fprintf('  transformer LV star neutral. No earthing transformer required.\n\n');

% Target earth fault current — high impedance earthing (Energex 11 kV standard)
I_ef_target = 100;   % A — target prospective earth fault current at NER
V_NER       = V_LV / sqrt(3);  % Phase-to-neutral voltage at transformer LV neutral

R_NER       = V_NER / I_ef_target;
P_NER       = I_ef_target^2 * R_NER;  % NER power dissipation (W)

fprintf('  NER Phase-to-Neutral Voltage: %.1f V\n', V_NER);
fprintf('  Target Earth Fault Current  : %d A\n',   I_ef_target);
fprintf('  Required NER Resistance     : %.1f Ohm\n', R_NER);
fprintf('  NER Power Rating (minimum)  : %.1f kW  (10 s rating per AS/NZS 2067)\n\n', P_NER/1e3);

%% ============================================================
%  PART 12: PLCC EQUIPMENT — COUPLING CAPACITORS & LMUs
%  Power Line Carrier Communication uses the 66 kV line as a
%  communication channel for protection signalling, SCADA, and
%  inter-bay tripping. Each incoming 66 kV line requires one
%  coupling capacitor (CC) and one line matching unit (LMU).
%  Standard: IEC 60358 (coupling capacitors)
% ============================================================

fprintf('--- PART 12: PLCC Equipment ---\n\n');

% PLCC equipment parameters
n_PLCC_lines   = 2;          % Number of 66 kV incoming lines with PLCC
CC_voltage_kV  = 72.5;       % Coupling capacitor rated voltage (kV) — matches Um
LMU_imp_src    = 75;         % LMU source impedance (Ohm) — line side
LMU_imp_load   = 400;        % LMU load impedance (Ohm) — carrier equipment side

fprintf('  PLCC Lines                  : %d (one per 66 kV incoming line)\n', n_PLCC_lines);
fprintf('  Coupling Capacitor Qty      : %d\n', n_PLCC_lines);
fprintf('  Coupling Capacitor Voltage  : %.1f kV  (IEC 60358, matches Um = 72.5 kV)\n', CC_voltage_kV);
fprintf('  Line Matching Unit Qty      : %d\n', n_PLCC_lines);
fprintf('  LMU Impedance               : %d / %d Ohm  (line / carrier equipment)\n\n', LMU_imp_src, LMU_imp_load);

fprintf('  NOTE: PLCC enables high-speed protection signalling over the\n');
fprintf('  66 kV line (e.g. permissive overreach transfer trip, POTT scheme)\n');
fprintf('  without requiring a separate pilot cable or fibre link.\n');
fprintf('  Carrier frequency range: typically 30–500 kHz per IEC 60495.\n\n');

%% ============================================================
%  PART 13: FINAL SUMMARY & EQUIPMENT SCHEDULE
% ============================================================

fprintf('============================================================\n');
fprintf('  FINAL SUMMARY — EQUIPMENT SCHEDULE\n');
fprintf('  66/11 kV Receiving Substation | Brisbane QLD\n');
fprintf('============================================================\n\n');

fprintf('SYSTEM PARAMETERS\n');
fprintf('  Nominal Voltage             : 66 / 11 kV\n');
fprintf('  Maximum Equipment Voltage   : 72.5 / 12.5 kV  (IEC 60694)\n');
fprintf('  Frequency                   : 50 Hz\n');
fprintf('  Present Peak Load           : %.1f MVA\n',   S_peak_now/1e6);
fprintf('  20-Year Design Load         : %.1f MVA\n',   S_design/1e6);
fprintf('  Configuration               : Single busbar with bus-sectionalisation, 2 bays, N-1\n\n');

fprintf('TRANSFORMERS (x2)\n');
fprintf('  Rating                      : %.0f MVA (each)\n', TX_rating/1e6);
fprintf('  Voltage Ratio               : 66/11 kV\n');
fprintf('  Vector Group                : Dyn11\n');
fprintf('  Impedance                   : %.0f%%\n',  Z_pct*100);
fprintf('  OLTC Range                  : +/-%.0f%%, %.2f%% steps\n', OLTC_range*100, OLTC_step*100);
fprintf('  Cooling                     : ONAN/ONAF\n\n');

fprintf('FAULT LEVELS\n');
fprintf('  11 kV Fault (single TX)     : %.2f kA\n',   I_fault_A_1/1e3);
fprintf('  11 kV Fault (both TXs)      : %.2f kA\n',   I_fault_A_2/1e3);
fprintf('  11 kV Limit (Energex)       : 25.00 kA\n');
fprintf('  66 kV Fault Current         : %.2f kA\n\n', Isc_HV/1e3);

fprintf('CIRCUIT BREAKERS\n');
fprintf('  66 kV Line CB (x2)          : 72.5 kV, 1250 A, 31.5 kA, SF6  (IEC 62271-100)\n');
fprintf('  66 kV Bus-Section CB (x1)   : 72.5 kV, 1250 A, 31.5 kA, SF6, normally open (NO)\n');
fprintf('  11 kV Incomer CB (x2)       : 12.5 kV, 31.5 kA, VCB, 2500 A  (IEC 62271-100)\n');
fprintf('  11 kV Feeder CB (x4)        : 12.5 kV, 31.5 kA, VCB, 630 A\n');
fprintf('  11 kV Solar Incomer CB (x1) : 12.5 kV, 31.5 kA, VCB, 800 A\n');
fprintf('  11 kV Bus-Section CB (x1)   : 12.5 kV, 31.5 kA, VCB, normally closed (NC)\n\n');

fprintf('DISCONNECTORS\n');
fprintf('  66 kV With Earth Switch (x2): 72.5 kV, 1250 A, motorised  (IEC 62271-102)\n');
fprintf('  66 kV Without Earth Sw (x6) : 72.5 kV, 1250 A, motorised\n\n');

fprintf('CURRENT TRANSFORMERS\n');
fprintf('  66 kV CT (x2 per bay, x10 total): %d/1 A, 5P20 (prot) / 0.2S (metering)\n', CT_HV_selected);
fprintf('  11 kV Incomer CT (x2)       : %d/1 A, 5P20 (prot) / 0.5 (metering)\n',  CT_LV_selected);
fprintf('  11 kV Feeder CT (x4)        : %d/1 A, 5P20 (protection)\n', CT_feeder_sel);
fprintf('  11 kV Solar CT (x1)         : 800/1 A, 5P20 / 0.5 (metering)\n\n');

fprintf('PLCC EQUIPMENT\n');
fprintf('  Coupling Capacitor (x2)     : 72.5 kV, one per 66 kV incoming line  (IEC 60358)\n');
fprintf('  Line Matching Unit (x2)     : 75/400 Ohm impedance matching, one per CC\n\n');

fprintf('SURGE ARRESTERS\n');
fprintf('  66 kV Line Arresters (x6)   : 60 kV rated, ZnO, IEC 60099-4\n');
fprintf('  11 kV Bus Arresters         : 10 kV rated, ZnO, IEC 60099-4\n\n');

fprintf('BUSBARS\n');
fprintf('  11 kV Main Busbar           : %d mm² aluminium tubular\n', A_selected);
fprintf('  Continuous Rating           : %.0f A\n',     I_LV_rated);
fprintf('  Fault Withstand             : %.2f kA, %.0f s\n\n', Isc_LV_design/1e3, t_fault);

fprintf('NEUTRAL EARTHING RESISTOR\n');
fprintf('  Connection                  : Direct to Dyn11 transformer LV star neutral\n');
fprintf('  No earthing transformer required (Dyn11 provides natural neutral point)\n');
fprintf('  NER Resistance              : %.1f Ohm\n',   R_NER);
fprintf('  Earth Fault Current (max)   : %d A\n',      I_ef_target);
fprintf('  NER Power Rating            : %.1f kW (10 s)\n\n', P_NER/1e3);

fprintf('APPLICABLE STANDARDS\n');
fprintf('  AS/NZS 2067     — Substations and HV installations >1 kV\n');
fprintf('  AS/NZS 4777.2   — Grid connection of energy systems via inverters (DG)\n');
fprintf('  IEC 60909        — Short-circuit current calculations\n');
fprintf('  IEC 62271-100    — AC circuit breakers\n');
fprintf('  IEC 62271-102    — AC disconnectors and earthing switches\n');
fprintf('  IEC 60076-1      — Power transformers\n');
fprintf('  IEC 60044-1      — Current transformers\n');
fprintf('  IEC 60865-1      — Busbar thermal/mechanical withstand\n');
fprintf('  IEC 60358        — Coupling capacitors and capacitor dividers\n');
fprintf('  Energex NCR      — Network connection requirements (QLD)\n');
fprintf('============================================================\n');
