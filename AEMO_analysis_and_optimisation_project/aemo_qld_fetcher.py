"""
AEMO QLD Live Data Fetcher — v8
====================================
Fetches real-time Queensland NEM data:
  - Spot price + demand    from DREGION table in Dispatch_Reports
  - Per-unit SCADA output  from Dispatch_SCADA (UNIT_SCADA table)
  - Fuel mix restricted to QLD1 registered generators only

DUID map and installed capacities sourced exclusively from:
  AEMO Generators Registration list — QLD1 region, April 2026
  (Power_Data.xlsx — 79 registered units)

Fuel types: coal, gas, hydro, wind, solar, oil, bagasse

── DISPATCH SIGNAL LOGIC (v8) ─────────────────────────────────────────────────
Arduino levels are no longer simple utilisation ratios.
They are DISPATCH ADJUSTMENT SIGNALS telling each controllable source how much
to ramp relative to its current output, based on the grid supply/demand balance.

  surplus_ratio  = surplus_mw / demand_mw
  TARGET_SURPLUS = 0.05  (5% above demand — keeps price slightly positive,
                           provides investor return without gouging consumers)

  adjustment = surplus_ratio - TARGET_SURPLUS
    +ve → oversupply  → ramp DOWN controllable sources
    -ve → undersupply → ramp UP  controllable sources

  Each controllable source gets a proportional share of the adjustment,
  weighted by its current share of total controllable output.
  Fast sources (gas, hydro, oil) receive the full adjustment signal.
  Slow sources (coal, bagasse)   receive a dampened signal (× SLOW_RAMP_FACTOR)
  to reflect their multi-hour startup/ramp constraints.

  The output level = current_utilisation − (share_weight × adjustment)
  Clamped to [0.0, 1.0].

Non-dispatchable sources (wind, solar) always report current utilisation only —
they cannot be ramped by instruction.

Run:
    pip install requests pyserial
    python aemo_qld_fetcher.py           # print only
    python aemo_qld_fetcher.py --once    # single fetch and exit
    python aemo_qld_fetcher.py --debug   # verbose logging
    python aemo_qld_fetcher.py --port COM3
    python aemo_qld_fetcher.py --port /dev/ttyUSB0
"""

import requests, zipfile, io, re, json, time, datetime, logging, argparse
from dataclasses import dataclass
from typing import Optional

try:
    import serial
    SERIAL_AVAILABLE = True
except ImportError:
    SERIAL_AVAILABLE = False

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger(__name__)

REGION        = "QLD1"
SERIAL_BAUD   = 9600
POLL_INTERVAL = 300
NEMWEB_BASE   = "https://www.nemweb.com.au/Reports/CURRENT"

# ── Dispatch signal parameters ────────────────────────────────────────────────
# Target 5% surplus above demand.
#   - Keeps spot price gently positive (healthy investor signal)
#   - Avoids the deep negative prices that destroy generator revenue
#   - Stays well below the $300/MWh cap contract threshold (consumer protection)
TARGET_SURPLUS_RATIO = 0.05

# Coal and bagasse are slow-ramp baseload sources (hours to adjust).
# Dampen their adjustment signal to reflect real-world ramp rate limits.
SLOW_RAMP_FACTOR = 0.25

# Fuel dispatchability classification
FAST_DISPATCH  = {"gas", "hydro", "oil"}       # minutes to respond
SLOW_DISPATCH  = {"coal", "bagasse"}            # hours to respond
NON_DISPATCH   = {"wind", "solar"}             # cannot be instructed

@dataclass
class GeneratorUnit:
    duid: str
    fuel_type: str
    output_mw: float

@dataclass
class QldSnapshot:
    timestamp: str
    interval: str
    spot_price: float
    demand_mw: float
    scheduled_gen_mw: float
    semi_scheduled_mw: float
    total_gen_mw: float
    surplus_mw: float
    fuel_mix: dict
    units: list
    price_signal: str

# ── CSV Parser ────────────────────────────────────────────────────────────────
def parse_nemweb_csv(text: str, table_name: str) -> list[dict]:
    headers  = None
    rows     = []
    in_table = False
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        parts = [p.strip().strip('"') for p in line.split(",")]
        if len(parts) < 3:
            continue
        if parts[0] == "I":
            t1 = parts[1].upper()
            t2 = parts[2].upper() if len(parts) > 2 else ""
            if t1 == table_name.upper() or t2 == table_name.upper():
                headers  = [h.strip() for h in parts[4:]]
                in_table = True
            else:
                in_table = False
        elif parts[0] == "D" and in_table and headers:
            values = parts[4:]
            rows.append({headers[i]: values[i].strip() if i < len(values) else ""
                         for i in range(len(headers))})
    return rows

# ── Zip Fetcher ───────────────────────────────────────────────────────────────
def fetch_latest_zip_text(report_folder: str, filename_filter: str = "") -> Optional[str]:
    index_url = f"{NEMWEB_BASE}/{report_folder}/"
    try:
        resp = requests.get(index_url, timeout=15)
        resp.raise_for_status()
    except Exception as e:
        log.error(f"Index fetch failed [{report_folder}]: {e}")
        return None
    zips = re.findall(r'href="([^"]+\.zip)"', resp.text, re.IGNORECASE)
    if not zips:
        log.warning(f"No zips at {index_url}")
        return None
    if filename_filter:
        filtered = [z for z in zips if filename_filter.upper() in z.upper().split("/")[-1]]
        if not filtered:
            log.warning(f"No zips matching '{filename_filter}'")
            return None
        zips = filtered
    latest   = sorted(zips)[-1]
    file_url = ("https://www.nemweb.com.au" + latest) if latest.startswith("/") else latest
    log.info(f"Downloading: {file_url}")
    try:
        r = requests.get(file_url, timeout=30)
        r.raise_for_status()
    except Exception as e:
        log.error(f"Download failed: {e}")
        return None
    try:
        with zipfile.ZipFile(io.BytesIO(r.content)) as z:
            csv_name = next((n for n in z.namelist() if n.upper().endswith(".CSV")), None)
            if not csv_name:
                return None
            log.info(f"Parsing: {csv_name}")
            with z.open(csv_name) as f:
                return f.read().decode("utf-8", errors="replace")
    except Exception as e:
        log.error(f"Unzip error: {e}")
        return None

# ── Region Data (price + demand) ──────────────────────────────────────────────
def get_region_data(region: str = REGION):
    text = fetch_latest_zip_text("Dispatch_Reports", "PUBLIC_DISPATCH")
    if not text:
        return None, None, None, None
    rows = parse_nemweb_csv(text, "DREGION")
    log.info(f"DREGION rows: {len(rows)}")
    region_rows = [r for r in rows if r.get("REGIONID", "").upper() == region.upper()]
    if not region_rows:
        log.warning(f"No DREGION rows for {region}")
        return None, None, None, None
    live   = [r for r in region_rows if r.get("INTERVENTION", "0") == "0"] or region_rows
    latest = sorted(live, key=lambda r: r.get("SETTLEMENTDATE", ""))[-1]
    try:
        return (float(latest.get("RRP",                    0) or 0),
                float(latest.get("TOTALDEMAND",            0) or 0),
                float(latest.get("DISPATCHABLEGENERATION", 0) or 0),
                latest.get("SETTLEMENTDATE", ""))
    except ValueError as e:
        log.error(f"DREGION parse error: {e}")
        return None, None, None, None

# ── QLD1 DUID → Fuel Map ──────────────────────────────────────────────────────
# Source: AEMO Generators Registration list, QLD1 region, April 2026
#         (Power_Data.xlsx — 79 registered units, exact AEMO DUID spelling)
QLD_DUID_MAP = {
    # ── Bagasse (57.0 MW total installed) ────────────────────────────────────
    "ICSM":       "bagasse",  # ISIS Central Sugar Mill        25.000 MW
    "EDLRGNRD":   "bagasse",  # Roghan Road                     2.000 MW
    "RPCG":       "bagasse",  # Rocky Point                    30.000 MW

    # ── Coal (8149.0 MW total installed) ─────────────────────────────────────
    "CPP_3":      "coal",     # Callide C                     420.000 MW
    "CPP_4":      "coal",     # Callide C                     420.000 MW
    "CALL_A_4":   "coal",     # Callide A                      30.000 MW
    "CALL_B_1":   "coal",     # Callide B                     350.000 MW
    "CALL_B_2":   "coal",     # Callide B                     350.000 MW
    "GSTONE1":    "coal",     # Gladstone                     280.000 MW
    "GSTONE2":    "coal",     # Gladstone                     280.000 MW
    "GSTONE3":    "coal",     # Gladstone                     280.000 MW
    "GSTONE4":    "coal",     # Gladstone                     280.000 MW
    "GSTONE5":    "coal",     # Gladstone                     280.000 MW
    "GSTONE6":    "coal",     # Gladstone                     280.000 MW
    "KPP_1":      "coal",     # Kogan Creek                   744.000 MW
    "MPP_1":      "coal",     # Millmerran                    426.000 MW
    "MPP_2":      "coal",     # Millmerran                    426.000 MW
    "STAN-1":     "coal",     # Stanwell                      365.000 MW
    "STAN-2":     "coal",     # Stanwell                      365.000 MW
    "STAN-3":     "coal",     # Stanwell                      365.000 MW
    "STAN-4":     "coal",     # Stanwell                      365.000 MW
    "TARONG#1":   "coal",     # Tarong                        350.000 MW
    "TARONG#2":   "coal",     # Tarong                        350.000 MW
    "TARONG#3":   "coal",     # Tarong                        350.000 MW
    "TARONG#4":   "coal",     # Tarong                        350.000 MW
    "TNPS1":      "coal",     # Tarong North                  443.000 MW

    # ── Gas (3226.0 MW total installed) ──────────────────────────────────────
    "MORANBAH":   "gas",      # Moranbah Generation Project    12.560 MW
    "YABULU":     "gas",      # Townsville Gas Turbine        160.000 MW
    "YABULU2":    "gas",      # Townsville Gas Turbine         82.000 MW
    "DAANDINE":   "gas",      # Daandine Power Station         33.000 MW
    "BRAEMAR5":   "gas",      # Braemar 2                     173.000 MW
    "BRAEMAR6":   "gas",      # Braemar 2                     173.000 MW
    "BRAEMAR7":   "gas",      # Braemar 2                     173.000 MW
    "BRAEMAR1":   "gas",      # Braemar                       168.000 MW
    "BRAEMAR2":   "gas",      # Braemar                       168.000 MW
    "BRAEMAR3":   "gas",      # Braemar                       168.000 MW
    "OAKY2":      "gas",      # Oaky Creek 2                   15.000 MW
    "OAKYCREK":   "gas",      # Oaky Creek                     20.000 MW
    "GERMCRK":    "gas",      # German Creek                   44.976 MW
    "BPLANDF1":   "gas",      # Browns Plains LFG               2.000 MW
    "GROSV1":     "gas",      # Grosvenor 1                    21.287 MW
    "GROSV2":     "gas",      # Grosvenor 2                    15.205 MW
    "MBAHNTH":    "gas",      # Moranbah North                 63.420 MW
    "BARCALDN":   "gas",      # Barcaldine                     37.000 MW
    "OAKEY1":     "gas",      # Oakey                         141.000 MW
    "OAKEY2":     "gas",      # Oakey                         141.000 MW
    "STAPYLTON1": "gas",      # Stapylton                       2.140 MW
    "DDPS1":      "gas",      # Darling Downs                 643.000 MW
    "ROCHEDAL":   "gas",      # Rochedale                       4.400 MW
    "ROMA_7":     "gas",      # Roma                           40.000 MW
    "ROMA_8":     "gas",      # Roma                           40.000 MW
    "WHIT1":      "gas",      # Whitwood                        1.000 MW
    "CPSA":       "gas",      # Condamine                     143.000 MW
    "YARWUN_1":   "gas",      # Yarwun                        154.000 MW
    "SWAN_E":     "gas",      # Swanbank E                    385.000 MW
    "TITREE":     "gas",      # Ti Tree                         2.000 MW

    # ── Hydro (655.7 MW total installed) ─────────────────────────────────────
    "W/HOE#1":    "hydro",    # Wivenhoe                      250.000 MW
    "W/HOE#2":    "hydro",    # Wivenhoe                      250.000 MW
    "BARRON-1":   "hydro",    # Barron Gorge                   30.000 MW
    "BARRON-2":   "hydro",    # Barron Gorge                   30.000 MW
    "KAREEYA1":   "hydro",    # Kareeya                        21.000 MW
    "KAREEYA2":   "hydro",    # Kareeya                        21.000 MW
    "KAREEYA3":   "hydro",    # Kareeya                        21.000 MW
    "KAREEYA4":   "hydro",    # Kareeya                        21.000 MW
    "KAREEYA5":   "hydro",    # Kareeya                         7.000 MW
    "WIVENSH":    "hydro",    # Wivenhoe Small Hydro            4.700 MW

    # ── Oil / diesel (450.0 MW total installed) ───────────────────────────────
    "MSTUART1":   "oil",      # Mt Stuart                     144.000 MW
    "MSTUART2":   "oil",      # Mt Stuart                     144.000 MW
    "MSTUART3":   "oil",      # Mt Stuart                     131.000 MW
    "STHBKTEC":   "oil",      # Southbank                       1.000 MW
    "MACKAYGT":   "oil",      # Mackay GT                      30.000 MW

    # ── Solar (351.232 MW total installed) ───────────────────────────────────
    "CLARESF1":   "solar",    # Clare Solar Farm              110.262 MW
    "BARCSF1":    "solar",    # Barcaldine Solar               20.000 MW
    "LRSF1":      "solar",    # Longreach Solar                17.000 MW
    "VALDORA1":   "solar",    # Valdora Solar                  15.000 MW
    "KSP1":       "solar",    # Kidston Solar                  50.000 MW
    "HUGSF1":     "solar",    # Hughenden Solar                20.970 MW
    "SMCSF1":     "solar",    # Sun Metals                    118.000 MW

    # ── Wind (12.0 MW total installed) ───────────────────────────────────────
    "WHILL1":     "wind",     # Windy Hill                     12.000 MW
}

# Fuel types that are semi-scheduled (variable / non-dispatchable)
SEMI_SCHEDULED_FUELS = {"wind", "solar"}

# ── SCADA Fetch ───────────────────────────────────────────────────────────────
def get_scada_units() -> list[GeneratorUnit]:
    text = fetch_latest_zip_text("Dispatch_SCADA", "DISPATCHSCADA")
    if not text:
        return []
    rows = parse_nemweb_csv(text, "UNIT_SCADA")
    if not rows:
        rows = parse_nemweb_csv(text, "SCADA")
    log.info(f"SCADA total rows: {len(rows)}")
    units = []
    for row in rows:
        duid = row.get("DUID", "").strip()
        fuel = QLD_DUID_MAP.get(duid)
        if fuel is None:
            continue
        try:
            mw = float(row.get("SCADAVALUE", 0) or 0)
        except ValueError:
            continue
        if mw > 0:
            units.append(GeneratorUnit(duid=duid, fuel_type=fuel, output_mw=round(mw, 1)))
    log.info(f"QLD1 registered units active: {len(units)}")
    return units

# ── Snapshot ──────────────────────────────────────────────────────────────────
def build_snapshot() -> QldSnapshot:
    log.info("─── Fetching QLD1 dispatch data ───")
    price, demand, sched, interval = get_region_data()
    units = get_scada_units()

    fuel_mix: dict[str, float] = {}
    for u in units:
        fuel_mix[u.fuel_type] = round(fuel_mix.get(u.fuel_type, 0) + u.output_mw, 1)

    price     = price  or 0.0
    demand    = demand or 0.0
    semi       = round(sum(v for k, v in fuel_mix.items() if k in SEMI_SCHEDULED_FUELS), 1)
    total_gen  = round(sched, 1) if sched else round(sum(fuel_mix.values()), 1)
    sched_disp = round(total_gen - semi, 1)
    surplus    = round(total_gen - demand, 1)
    interval   = interval or datetime.datetime.now().isoformat()

    if   price < -100: signal = "SEVERE_OVERSUPPLY"
    elif price < 0:    signal = "OVERSUPPLY"
    elif price < 100:  signal = "BALANCED"
    elif price < 300:  signal = "ELEVATED"
    elif price < 1000: signal = "HIGH_DEMAND"
    else:              signal = "PRICE_SPIKE"

    return QldSnapshot(
        timestamp         = datetime.datetime.now().isoformat(),
        interval          = interval,
        spot_price        = round(price, 2),
        demand_mw         = round(demand, 1),
        scheduled_gen_mw  = sched_disp,
        semi_scheduled_mw = semi,
        total_gen_mw      = total_gen,
        surplus_mw        = surplus,
        fuel_mix          = fuel_mix,
        units             = sorted(units, key=lambda u: -u.output_mw),
        price_signal      = signal,
    )

# ── Installed Capacity ────────────────────────────────────────────────────────
# Source: AEMO Generators Registration list, QLD1 region, April 2026
QLD_CAPACITY = {
    "coal":     8149.000,
    "gas":      3226.000,
    "hydro":     655.700,
    "oil":       450.000,
    "bagasse":    57.000,
    "solar":     351.232,
    "wind":       12.000,
}

# ── Arduino Dispatch Command (v8) ─────────────────────────────────────────────
def build_arduino_command(snap: QldSnapshot) -> dict:
    """
    Calculates a dispatch adjustment signal for each controllable fuel source.

    Goal: bring surplus to TARGET_SURPLUS_RATIO (5% above demand).
      - Keeps spot price gently positive → investor return signal
      - Avoids oversupply that drives prices deeply negative
      - Stays well below the $300/MWh consumer protection threshold

    Output levels represent TARGET utilisation [0.0–1.0] after adjustment,
    not raw current utilisation. The Arduino should drive actuators to these
    target levels, not add them to current state.

    Non-dispatchable sources (wind, solar) report current utilisation only —
    they are read-only and carry no adjustment.
    """
    demand   = snap.demand_mw if snap.demand_mw > 0 else 1.0
    surplus  = snap.surplus_mw

    # How far are we from the target?
    # +ve → more surplus than needed → ramp down
    # -ve → deficit or insufficient surplus → ramp up
    surplus_ratio = surplus / demand
    adjustment    = surplus_ratio - TARGET_SURPLUS_RATIO  # signed

    log.info(
        f"Dispatch calc: surplus={surplus:.1f} MW  "
        f"ratio={surplus_ratio:.3f}  target={TARGET_SURPLUS_RATIO}  "
        f"adj={adjustment:+.3f}"
    )

    # Current utilisation per fuel type
    current_util = {
        fuel: snap.fuel_mix.get(fuel, 0.0) / cap
        for fuel, cap in QLD_CAPACITY.items()
    }

    # Total controllable output (MW) — fast + slow dispatch only
    controllable_fuels = FAST_DISPATCH | SLOW_DISPATCH
    total_controllable_mw = sum(
        snap.fuel_mix.get(f, 0.0) for f in controllable_fuels
    )

    levels    = {}
    breakdown = {}  # for display

    for fuel, cap in QLD_CAPACITY.items():
        util = current_util[fuel]

        if fuel in NON_DISPATCH:
            # Wind/solar: report current output only, no adjustment
            levels[fuel]    = round(util, 3)
            breakdown[fuel] = {"util": round(util, 3), "adj": 0.0, "target": round(util, 3)}
            continue

        current_mw = snap.fuel_mix.get(fuel, 0.0)

        if total_controllable_mw > 0 and current_mw > 0:
            # Proportional share of this fuel's contribution to controllable output.
            # A source generating more MW carries more of the adjustment burden.
            share = current_mw / total_controllable_mw
        else:
            share = 0.0

        # Apply ramp rate dampening for slow sources
        ramp_factor = SLOW_RAMP_FACTOR if fuel in SLOW_DISPATCH else 1.0

        # Adjustment in utilisation units:
        # If adjustment > 0 (oversupply), reduce utilisation
        # If adjustment < 0 (undersupply), increase utilisation
        util_delta = share * adjustment * ramp_factor

        target_util = round(max(0.0, min(1.0, util - util_delta)), 3)

        levels[fuel]    = target_util
        breakdown[fuel] = {
            "util":   round(util, 3),
            "share":  round(share, 3),
            "adj":    round(-util_delta, 3),   # +ve means "ramp up", -ve means "ramp down"
            "target": target_util,
        }

    log.info(f"Dispatch breakdown: {json.dumps(breakdown)}")

    return {
        "cmd":          "DISPATCH",
        "signal":       snap.price_signal,
        "price":        snap.spot_price,
        "surplus_mw":   snap.surplus_mw,
        "surplus_pct":  round(surplus_ratio * 100, 1),
        "target_pct":   round(TARGET_SURPLUS_RATIO * 100, 1),
        "adjustment":   round(adjustment, 4),
        "levels":       levels,
        "breakdown":    breakdown,
    }

def send_to_arduino(cmd: dict, port: str):
    try:
        with serial.Serial(port, SERIAL_BAUD, timeout=3) as ser:
            time.sleep(3)
            ready = ser.readline().decode().strip()
            log.info(f"Arduino: {ready}")
            payload_cmd = {k: v for k, v in cmd.items() if k != "breakdown"}
            payload = json.dumps(payload_cmd) + "\n"
            ser.write(payload.encode())
            log.info(f"→ Arduino: {payload.strip()}")
            ack = ser.readline().decode().strip()
            if ack:
                log.info(f"← ACK: {ack}")
    except Exception as e:
        log.error(f"Serial error: {e}")

# ── Display ───────────────────────────────────────────────────────────────────
C = {
    "SEVERE_OVERSUPPLY": "\033[94m", "OVERSUPPLY":  "\033[96m",
    "BALANCED":          "\033[92m", "ELEVATED":    "\033[93m",
    "HIGH_DEMAND":       "\033[33m", "PRICE_SPIKE": "\033[91m",
    "RST":  "\033[0m",  "CYAN": "\033[96m", "RED": "\033[91m",
    "GRN":  "\033[92m", "YLW": "\033[93m",  "DIM": "\033[2m",
}

FUEL_ORDER = ["coal", "gas", "hydro", "wind", "solar", "oil", "bagasse"]

def print_snapshot(snap: QldSnapshot):
    sc    = C.get(snap.price_signal, "")
    rst   = C["RST"]
    sur_c = C["CYAN"] if snap.surplus_mw >= 0 else C["RED"]
    label = "SURPLUS" if snap.surplus_mw >= 0 else "DEFICIT"

    print("\n" + "═"*66)
    print(f"  QLD NEM  ─  {snap.timestamp}")
    print(f"  Interval : {snap.interval}")
    print("═"*66)
    print(f"  Spot Price      : {sc}${snap.spot_price:>10.2f} /MWh  [{snap.price_signal}]{rst}")
    print(f"  Demand          : {snap.demand_mw:>10.1f} MW")
    print(f"  Scheduled Gen   : {snap.scheduled_gen_mw:>10.1f} MW")
    print(f"  Semi-sched Gen  : {snap.semi_scheduled_mw:>10.1f} MW  (wind + solar)")
    print(f"  Total Gen       : {snap.total_gen_mw:>10.1f} MW  (AEMO DISPATCHABLEGENERATION)")
    print(f"  Surplus/Deficit : {sur_c}{snap.surplus_mw:>+10.1f} MW  ({label}){rst}")

    if snap.fuel_mix:
        print("\n  FUEL MIX  (QLD1 registered units only):")
        mix_total = sum(snap.fuel_mix.values()) or 1
        max_mw    = max(snap.fuel_mix.values(), default=1)
        ordered   = [f for f in FUEL_ORDER if f in snap.fuel_mix]
        extras    = [f for f in snap.fuel_mix if f not in FUEL_ORDER]
        for fuel in ordered + extras:
            mw   = snap.fuel_mix[fuel]
            cap  = QLD_CAPACITY.get(fuel, 1)
            pct  = mw / mix_total * 100
            util = mw / cap * 100
            bar  = "█" * int((mw / max_mw) * 20)
            print(f"    {fuel:<10} {mw:>7.1f} MW  {pct:5.1f}%  util {util:5.1f}%  {bar}")
        print(f"    {'─'*10} {mix_total:>7.1f} MW  100.0%")

    if snap.units:
        print(f"\n  TOP UNITS  ({len(snap.units)} QLD1 registered units active):")
        for u in snap.units[:15]:
            print(f"    {u.duid:<15} {u.fuel_type:<10} {u.output_mw:>7.1f} MW")
    print("═"*66)


def print_dispatch(cmd: dict):
    """Pretty-print the dispatch command with human-readable adjustment context."""
    rst  = C["RST"]
    sur  = cmd["surplus_pct"]
    tgt  = cmd["target_pct"]
    adj  = cmd["adjustment"]

    sur_c = C["CYAN"] if sur >= tgt else C["RED"]
    adj_c = C["YLW"]  if abs(adj) > 0.01 else C["GRN"]

    print(f"\n  {'─'*62}")
    print(f"  DISPATCH SIGNAL  [{cmd['signal']}]")
    print(f"  {'─'*62}")
    print(f"  Spot Price  : ${cmd['price']:>8.2f} /MWh")
    print(f"  Surplus     : {sur_c}{sur:>+6.1f}%{rst}  (target {tgt:+.1f}%)")
    print(f"  Adjustment  : {adj_c}{adj:>+.4f}{rst}  "
          f"({'ramp down' if adj > 0 else 'ramp up' if adj < 0 else 'hold'})")
    print()
    print(f"  {'Fuel':<10}  {'Current':>8}  {'Δ':>7}  {'Target':>8}  {'Type':<12}")
    print(f"  {'─'*10}  {'─'*8}  {'─'*7}  {'─'*8}  {'─'*12}")
    for fuel in FUEL_ORDER:
        if fuel not in cmd["breakdown"]:
            continue
        b    = cmd["breakdown"][fuel]
        util = b["util"]
        targ = b["target"]
        delta= b["adj"]
        if fuel in NON_DISPATCH:
            ftype = "non-dispatch"
            dc    = C["DIM"]
        elif fuel in FAST_DISPATCH:
            ftype = "fast dispatch"
            dc    = C["GRN"] if delta >= 0 else C["RED"]
        else:
            ftype = "slow dispatch"
            dc    = C["YLW"] if delta >= 0 else C["RED"]

        dc2 = C["GRN"] if delta >= 0 else C["RED"]
        print(f"  {fuel:<10}  {util:>7.1%}  {dc2}{delta:>+6.1%}{rst}  {targ:>7.1%}  {C['DIM']}{ftype}{rst}")
    print(f"  {'─'*62}")


# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="AEMO QLD1 Live Fetcher v8")
    parser.add_argument("--port",default="COM6",help="Arduino serial port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--once",  action="store_true", help="Single fetch then exit")
    parser.add_argument("--debug", action="store_true", help="Verbose logging")
    args = parser.parse_args()
    if args.debug:
        logging.getLogger().setLevel(logging.DEBUG)

    print("AEMO QLD1 Live Fetcher (v8)")
    print(f"Region : {REGION}  |  Registered units: {len(QLD_DUID_MAP)}")
    print(f"Poll   : {POLL_INTERVAL}s  |  Arduino: {args.port or 'not connected'}")
    print(f"Target : {TARGET_SURPLUS_RATIO*100:.0f}% surplus above demand")
    print("Ctrl+C to stop\n")

    while True:
        try:
            snap = build_snapshot()
            print_snapshot(snap)
            cmd  = build_arduino_command(snap)
            print_dispatch(cmd)
            if args.port:
                send_to_arduino(cmd, port=args.port)
            else:
                # Print the serial payload (without verbose breakdown)
                serial_payload = {k: v for k, v in cmd.items() if k != "breakdown"}
                print(f"\n  [SERIAL PAYLOAD]\n{json.dumps(serial_payload, indent=4)}")
        except KeyboardInterrupt:
            print("\nStopped.")
            break
        except Exception as e:
            log.error(f"Error: {e}", exc_info=args.debug)
        if args.once:
            break
        print(f"\n  Next poll in {POLL_INTERVAL}s...")
        time.sleep(POLL_INTERVAL)

if __name__ == "__main__":
    main()