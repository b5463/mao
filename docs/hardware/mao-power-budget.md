# MAO_MAIN A1 power budget

Estimates from datasheet values at 25 °C, before measurement. Bring-up measures every row through the two 0 Ω
links: **LINK_BAT** (R108, the whole board's battery current) and **LINK_REG** (R122, the TPS62840 and everything on
+3V3), ODD JOBS 110. The A0 budget (85 µA deep sleep) is in git history; A1 replaces it.

## Assumptions

- Cell: 1S LiPo, 500 mAh (LP503035 class), 3.7 V nominal, own protection PCM (its quiescent current, typically a few
  µA, is the cell's, not the board's, and is listed separately).
- +3V3 = 3.2 V from the TPS62840 (VSET 102 kΩ). Efficiency from SLVSEC6D Fig. 19 (VOUT 3.3 V, PFM, read off the
  graph, ±1 %): about 87 % at 10–100 µA with VIN 3.6 V, 89–91 % at 4.2 V; ~90 % at 10–300 mA. Below VIN ≈ 3.25 V
  it runs in 100 % mode (battery current = load current). Battery current = I(3V3) × 3.2 / (η × VBAT).
- Amplifier, IR LEDs and the panel's backlight LEDs run from VSYS (= VBAT − I × RON_BAT on battery). The haptic
  driver is on +3V3 (Gate C).
- Radio: ESP-NOW listening with Wi-Fi power save off (today's firmware); the "radio PS" figures assume an ESP-NOW
  wake window (firmware choice).
- Backlight: constant-current sink, 28.7 mA at 100 % (94.6 mV / 3.3 Ω), linear in the LEDC duty.

## Deep sleep, per component

Wake sources armed: face press (GPIO1, ext1), dial channel A (GPIO2, ext0), IMU wake-on-motion (GPIO4, ext1),
RTC timer. Panel, backlight, amplifier, haptic driver, IR receiver and ToF off; every enable held off by its
hardware pull-down, so firmware may leave them Hi-Z.

**+3V3 side (µA, at 3.2 V)**

| Item | Typ | Max | Worst used | Source |
|---|---:|---:|---:|---|
| ESP32-S3 deep sleep, RTC memory + RTC peripherals on (ext0/ext1) | 8.0 | UNKNOWN | 12 | ESP32-S3 datasheet v2.2 Table 5-10 (typ only); allowance |
| ICM-42670-P accelerometer wake-on-motion (LP, 1.56 Hz) | 4.4 | UNKNOWN | 10 | TDK product page (not in DS-000451 r1.0/r1.2); 9.8 µA at 25 Hz from a TDK listing as the allowance |
| VL53L4CD, XSHUT low (HW standby) | 5.0 | 7.0 | 7.0 | DS13812 Rev 3 Table 12 (2.8 V) |
| DRV2605L, EN low | 4.0 | 7.0 | 7.0 | SLOS854D §6.5 (3.6 V) |
| 2 × DRV5012, SEL low (20 Hz) | 3.2 | 6.6 | 6.6 | SLVSDD5 §6.5, 1.6 / 3.3 µA each at 3.0 V (no 3.3 V row) |
| MAX17048 SDA/SCL pull-down (IPD) through the 4.7 k pull-ups | 0.4 | 0.8 | 0.8 | Gate C (MAX17048 datasheet) |
| 2 × TPS22916C off | 0.02 | 0.2 | 0.2 | SLVSDO5F §6.5 ISD 10 / 100 nA |
| IMU INT1, ToF INT, press, straps, STAT1/2, VBUS divider, enables | 0 | 0.1 | 0.1 | design: no pull is fighting a driven level |
| TLV9061, panel, IR receiver | 0 | 0 | 0 | unpowered (switched rails) |
| **Subtotal** | **25.0** | | **43.7** | |

**Battery side (µA)**

| Item | Typ | Max | Worst used | Source |
|---|---:|---:|---:|---|
| +3V3 loads converted (typ: 3.7 V, 87 %; worst: 3.6 V, 80 %) | 24.9 | | 48.6 | Fig. 19, with a 7-point allowance on the worst case |
| TPS62840 IQ (VIN + VOS) | 0.09 | 0.48 | 0.48 | SLVSEC6D §7.5 (−40…85 °C max) |
| BQ25185, battery only | 4.0 | 5.0 | 5.0 | SLUSF65B §5.5 IQ_BAT |
| MAX17048 hibernate (reset comparator off) | 3.0 | 5.0 | 5.0 | 19-6171 Rev 7 |
| MAX98357A shutdown | 0.6 | 2.0 | 2.0 | 19-6779 Rev 7 (specified at 5 V) |
| Backlight sink FET + LEDs (DMG2302UK IDSS) | 0 | 1.0 | 1.0 | DS38439 (16 V spec; far lower at 4 V) |
| IR LED switch (AO3400A IDSS) | 0 | 1.0 | 1.0 | AOS Rev 3.1 (30 V spec) |
| PCB and capacitor leakage | 1.0 | | 3.0 | allowance |
| **Board total** | **33.6** | | **66.1** | |
| Cell PCM (not the board) | ~2–4 | ~8 | | cell datasheet, at purchase |

**Reading:** 33.6 µA typical, under the 35 µA target, with little margin (the ToF's 5 µA standby is the
difference to Gate C's 31.4 µA). The every-maximum stack (66 µA) is over the 50 µA hard limit, as Gate C's was
(65 µA): two items have no published maximum (S3, IMU wake-on-motion) and carry allowances. **The 50 µA limit is a
measurement at LINK_BAT on the built board** (bring-up, current meter required). If it fails, the first levers are
the ToF (its supply could move to AUX_3V3 in A2: −5 µA) and the IMU ODR.

## Active and peak

| State | What runs | 3V3 load | Battery current | Runtime (500 mAh) |
|---|---|---:|---:|---:|
| Active | face 70 % (20 mA LEDs), animation, sensors, radio listening | ~112 mA | ~120 mA | ~4.2 h |
| Active, radio PS | as above, ESP-NOW wake window | ~55 mA | ~70 mA | ~7 h |
| Drowsy | panel sleep-in, light sleep, motion/press/dial wake, radio windowed | ~1.4 mA | ~1.4 mA | ~15 days |
| Deep sleep | wake sources only | 25 µA | 33.6 µA typ | ~1.7 years (self-discharge dominates) |

**+3V3 worst case against the TPS62840's 750 mA:** S3 Wi-Fi TX 355 mA + LRA 150 mA (allowance) + ToF ranging
24 mA max (DS13812) + panel 8.5 mA + IMU 0.55 mA + Hall pair fast 0.74 mA max + TLV9061 0.6 mA + IR receiver
1.2 mA max + pull-ups ~2.5 mA = **≈ 543 mA (207 mA margin)**; with the S3 at Espressif's "supply ≥ 0.5 A" rule
instead of its TX peak: **≈ 688 mA (62 mA margin, 8 %)**. Never reaches the 1.0 A minimum switch limit.

**VSYS worst case:** + amplifier 260 mA (0.77 W into 8 Ω at 3.7 V) + backlight 29 mA + IR 2 × 59 mA ≈ **950 mA**
from the cell: within the BQ25185 BATFET (3.1 A OCP), LINK_BAT (1206 0 Ω), Q1 (AO3401A, 4 A) and just under the
JST SH contact rating (1 A per contact): see the A1 report, battery connector.

## Low-battery floor (TPS62840 in 100 % mode)

CORE (+3V3) must stay ≥ 3.0 V at the load (S3 minimum). In 100 % mode
`+3V3 = VSYS − I3V3 × (R_HS + DCR + R_LINK_REG)` and `VSYS = VBAT − I_SYS × RON_BAT`; the gauge measures VBAT
after the reverse-polarity FET Q1, the cell sits before Q1 and LINK_BAT.

| Event | R | I3V3 | I_SYS | VBAT at the gauge, min | cell terminal, min (Q1 + LINK_BAT) |
|---|---|---:|---:|---:|---:|
| worst (TX + LRA + ToF + audio + IR + backlight) | max (R_HS 0.60, DCR 0.116, links 0.05, RON_BAT 0.14, Q1 0.085 Ω) | 543 mA | 950 mA | **3.56 V** | 3.69 V |
| worst | typ (0.43, 0.097, 0.02, 0.115, 0.06 Ω) | 543 mA | 950 mA | 3.42 V | 3.50 V |
| TX burst, no audio/haptics/IR | max | 390 mA | 420 mA | 3.37 V | 3.43 V |

Q1 (AO3401A, RDS(on) 60/85 mΩ typ/max at VGS −2.5 V, AOS Rev 3.1) drops 57 / 81 mV at the 950 mA worst event and
~9 mV at a typical 150 mA. It does not move the floor measured at the gauge (Q1 is upstream of it); it costs the
cell that much extra voltage during the worst peak, i.e. a few percent of the last capacity. **Kept** for ODD JOBS 51
(the SH lead is re-terminated by hand). Firmware floor: shutdown at 3.55 V (gauge, loaded), warning 3.65 V, and the
power HAL serialises audio + haptics + IR when VBAT < 3.7 V (Gate C rule); the BQ25185's 3.0 V BATFET cut-off
(BUVLO) is the hardware floor that replaces A0's UVLO divider.

## Charging

BQ25185: VBUS 500 mA input limit (ILIM/VSET 18 k), 4.2 V, 210 mA charge (ISET 1.43 k, 199–220 mA: the LP503035
allows 250 mA = 0.5C), power path (SYS 4.5 V on USB). Dissipation worst case (VIN 5 V, VBAT 3.0 V, 210 mA charge +
290 mA system): (5 − 3.0) × 0.21 + (5 − 4.5) × 0.29 ≈ 0.57 W, on F under the panel (same spot as A0's BQ24073,
0.72 W): the thermal-camera check of A0 bring-up §4 stays. Firmware pauses charging through /CE (GPIO18) outside
the cell's 0–45 °C; the charger's own TS window is the cell NTC on TS/MR.
