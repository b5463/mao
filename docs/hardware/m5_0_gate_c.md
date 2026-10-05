# MAO M5.0 — Gate C / Rev A Schematic Review

## Status

**Gate C: PASS, schematic complete, with the unresolved items and Gate D blockers
listed below.** There is no placement, routing or Gerber work.

The PCB order stays blocked until all of these are done:
- the physical S3 wake test and pull test;
- a panel sample is in hand;
- a battery is chosen;
- Gates D and E.

This document was written for:
- the owner, reviewing Rev A before Gate D;
- whoever places and routes the board next.

## Git

- **Branch:** `feat/mao-m5-rev-a`.
- **Base:** `23968e8` (Gate B.1). The Gate C commit is on top of it.
- **M4.1** is untouched (frozen at `8a4f67b`).
- **Unchanged:** no firmware, security or ODD BUS files.

## What Gate C produced

| Path | What |
|---|---|
| `hardware/mao_rev_a/schematic/` | The schematic, written as a **SKiDL netlist**: sheets 01–11 (`s01_usb.py` … `s10_s11.py`), part templates with datasheet pinouts (`parts_*.py`), nets and rail ranges (`nets.py`) |
| `schematic/checks.py` | Gate C electrical rules, on top of SKiDL's ERC: connectivity, voltage domain / abs-max, capacitor derating, hardware default-off, I2C, footprint pad-to-pin match, back-power exposure |
| `schematic/budget.py` | Generates the power tables (`out/budget.md`) |
| `schematic/make_footprints*.py` | Generates the custom footprints (`footprints/MAO_RevA.pretty`) |
| `out/mao_rev_a.net` | KiCad netlist, which is the Gate D import |
| `out/bom.csv` | The BOM: exact MPNs, tolerances, DNP, sheet, and the reason for each part |
| `out/erc.txt` | The ERC report, with categorised waivers, per-rail capacitance and the back-power list |

**How to build:** use a Python venv with `skidl==2.3.0`, then run `python schematic/build.py`. It exits 1 on any unwaived error. KiCad 10 libraries are read from `C:\Program Files\KiCad\10.0`.

**About the format:** it is a text schematic, with no drawn `.kicad_sch`. Every connection is reviewable in the sheet files and machine-checked. Drawn sheets for human review can be produced from the netlist in KiCad at Gate D; that is listed under Unresolved.

**The sheets:**

| # | Sheet | Main parts |
|---|---|---|
| 01 | USB-C input | USB4105-GF-A, 2 × 5.1 k Rd, TPD2E2U06 (D±), TPD1E10B06 (VBUS), VBUS_SENSE divider |
| 02 | Battery and charger | JST PH 3-pin, LINK_BAT, BQ25185 (ILIM 18 k, ISET 1.0 k), NTC / DNP 10 k, STAT pull-ups |
| 03 | CORE_3V3 and fuel gauge | LINK_REG, TPS62840DLC (VSET 267 k), 2.2 µH, MAX17048 |
| 04 | MCU | ESP32-S3-MINI-1-N8, 22 µF + 0.1 µF, EN RC, BOOT, USB 22 Ω, BOARD_ID, I2C 4.7 k |
| 05 | Display and backlight | TPS22916C (LCD), FH12-18S, SPI damping, RST pull-down, TLV9061 + DMG2302UK current sink |
| 06 | Encoder | EC11E, 470 k pulls (provisional), ESD, DNP RC |
| 07 | IMU | ICM-42670-P, 0x68, INT pull-ups |
| 08 | Haptics | DRV2605L, REG/VDD caps, EN/TRIG pull-downs, LRA lead connector |
| 09 | Audio | MAX98357A on SYS, gain option, EMI options, speaker lead connector |
| 10 | IR and RGB | 2 × TPS22916C (AUX_SYS, AUX_3V3), SK6805-EC20, TSOP75438, VSMB2943 + DMG2302UK |
| 11 | Service | test pads (the fixture pads are on 04/05/06/10) |

The board has 142 parts, 77 nets and 36 test pads. The BOM has 48 fitted lines, 12 DNP parts and 36 test pads.

## Locked architecture

These are unchanged from Gate B/B.1 plus the owner's Gate C decisions:
- MCU: ESP32-S3-MINI-1-N8.
- Display: Winstar WF0128BTYAA4DNN0.
- IMU: ICM-42670-P.
- Haptics: DRV2605L, powered, with EN low.
- Audio: MAX98357A on SYS.
- Charger: BQ25185.
- **CORE_3V3: TPS62840**. The TPS63802 is removed and not carried.
- Fuel gauge: MAX17048.
- Load switches: TPS22916.
- ODD BUS: unchanged.

**New at Gate C, each explained in its section:**
1. **The IR receiver moves to its own 3.3 V gate.**
   - AUX_PWR_EN drives two TPS22916C: AUX_SYS for the RGB LED and AUX_3V3 for the IR receiver.
   - It still costs one GPIO.
   - Reason: on a SYS-fed gate the receiver's internally pulled-up output would reach 4.5 V at IO34.
2. **Backlight: a constant-current sink from SYS.** It is not a plain PWM MOSFET.
3. **Battery connector: JST PH (2 A per contact).** SH and GH are rated 1 A, which is below the worst-case current.

## MCU

**ESP32-S3-MINI-1-N8** (MINI-1 datasheet v1.7):
- Supply: 3V3 3.0–3.6 V, and the supply must deliver at least 0.5 A (Table 6-2).
- Decoupling: 22 µF + 0.1 µF at pad 3, and EN RC 10 k / 1 µF (Fig 9-1).
- Straps:
  - **IO0:** 10 k pull-up and the BOOT pad.
  - **IO3:** floating.
  - **IO45:** internal pull-down. Nothing may pull it high, or VDD_SPI switches to 1.8 V.
  - **IO46:** internal pull-down.
- USB: 22 Ω series at the module and DNP 10 pF to GND (HDG §2.13).
- Brownout: IDF default level 7 (≈ 2.44 V). That is config, not hardware.

**IO47/IO48 domain: resolved.**
- They are in VDD_SPI or VDD3P3_CPU, chosen by eFuse (S3 datasheet v2.2 Table 2-1, note 3).
- **Both are 3.3 V on the N8:**
  - VDD_SPI is fed from VDD3P3_RTC through RSPI (≈ 14 Ω) with GPIO45 = 0 (MINI-1 Table 4-4, HDG §2.1.1);
  - the flash is a 3.3 V part;
  - 1.8 V applies only to the R8V/R16V chips.
- I2C on IO47/48 with 3.3 V pull-ups is therefore correct.

## Final GPIO map

The pins are unchanged from Gate B.1. States are shown below. "Visible rest" is the dim-logo light sleep: the panel stays on and the backlight is at 3 %.

| GPIO | Signal | Reset / boot | Active | Visible rest | Deep sleep |
|---|---|---|---|---|---|
| 0 | BOOT pad | strap: 10 k up | unused | — | — |
| 1 | ENC_SW | input, 470 k up | input + internal pull-up | GPIO wake | **ext1 ANY_LOW**, internal pulls off |
| 2 | ENC_A | input, 470 k up | input + internal pull-up | GPIO wake | **ext0**, armed at the opposite of its level |
| 3 | — | floating strap | — | — | — |
| 4 | IMU_INT1 | input, 100 k up | input | GPIO wake | **ext1 ANY_LOW** |
| 5 | IMU_INT2 | input, 100 k up | input | input | isolated |
| 6 | ENC_B | input, no external pull | input + internal pull-up | input + internal pull-up | isolated, pulls off |
| 7 | LCD_PWR_EN | Hi-Z → **switch off** (750 k) | high | high | Hi-Z / low → panel unpowered |
| 8 | LCD_BL | Hi-Z → **off** (100 k + 1 k) | LEDC ~30 kHz | LEDC 3 % | low / Hi-Z |
| 9 | LCD_TE | input | input | input | isolated |
| 10–13 | CS, MOSI, SCLK, DC | Hi-Z | SPI | idle | **low or Hi-Z, never high** |
| 14 | LCD_RST | Hi-Z → **reset** (100 k down) | high | high | low |
| 15 | BOARD_ID | input (read once) | — | — | isolated |
| 16 | CHG_STAT2 | input, 10 k up | input | input | high-Z on battery: 0 µA |
| 17 / 18 / 21 | I2S BCLK / WS / DIN | Hi-Z | I2S | low | low / Hi-Z |
| 19 / 20 | USB D− / D+ | USB | USB | USB (host → stays awake) | — |
| 26 | spare | — | — | — | — |
| 33 | RGB_DATA | eFuse default (RGB unpowered) | RMT | low | Hi-Z (aux off) |
| 34 | IR_RX | input | RMT in | input | input |
| 35 | VBUS_SENSE | input | input | input | 0 µA without USB |
| 36 | CHG_STAT1 | input, 10 k up | input | input | high-Z on battery |
| 37 | spare | — | — | — | — |
| 38 | AMP_SD | Hi-Z → **off** (internal 100 k) | high while playing | low | Hi-Z (off) |
| 39 | IR_TX | Hi-Z → **off** (100 k + gate 100 k) | RMT carrier | low | Hi-Z (off) |
| 40 | AUX_PWR_EN | Hi-Z → **off** (750 k × 2) | high when RGB/IR needed | low | Hi-Z (off) |
| 41 | HAPTIC_EN | Hi-Z → **off** (100 k + internal 2 M) | high while playing | low | Hi-Z (off) |
| 42 | HAPTIC_TRIG | Hi-Z → low (100 k) | low / pulse | low | Hi-Z |
| 43 / 44 | U0TXD / U0RXD, spare | the ROM prints on TX | factory log pads | — | — |
| 45 / 46 | straps | internal pull-downs | — | — | — |
| 47 / 48 | I2C SDA / SCL | Hi-Z, 4.7 k up | I2C 400 kHz | idle high | idle high (not held low: MAX17048 sleep rule) |

**Every enable is off in hardware**, so deep sleep can leave each one Hi-Z. No RTC hold is needed to keep a peripheral off.

## Spare GPIO

**4, re-verified: IO26, IO37, IO43, IO44.**
- **IO26:** free on the N8 (MINI-1 Table 3-1, footnote b: taken only on -N4R2). The S3 datasheet ranks it P4, "not recommended", in general terms; it's used as a spare pad only.
- **IO37:** in the IO33–37 group. Those pins are octal-flash/PSRAM lines only on octal parts, so they're free with the N8's quad flash. Its default function is set by eFuse.
- **IO43/44:** UART0. The ROM boot log appears on IO43 at reset. They're usable after boot.
- IO5 becomes a fifth spare if IMU INT2 is dropped.

Each spare has a test pad on sheet 04.

## Power domains

| Rail | Source | Range | Loads |
|---|---|---|---|
| VBUS | USB-C | 4.4–5.5 V | BQ25185 IN, VBUS_SENSE |
| VBAT | cell (via LINK_BAT) | 3.0–4.2 V | BQ25185 BAT, MAX17048 VDD/CELL |
| SYS | BQ25185 | 4.5 V on USB, else VBAT − I × RON_BAT | TPS62840, **MAX98357A**, **backlight LED**, **IR LED**, AUX_SYS |
| CORE_3V3 | TPS62840 (via LINK_REG) | 3.3 V; follows VIN in 100 % mode | S3, IMU, DRV2605L, I2C / STAT / encoder pulls, LCD and AUX_3V3 switches |
| LCD_3V3 | TPS22916C ← CORE_3V3 | 0 / 3.3 V | panel VCI, TLV9061 |
| AUX_SYS | TPS22916C ← SYS | 0 / SYS | SK6805-EC20 |
| AUX_3V3 | TPS22916C ← CORE_3V3 | 0 / 3.3 V | TSOP75438 (through its 100 Ω / 0.1 µF filter) |

**Segregation:**
- The amplifier, the backlight LED, the RGB and the IR LED are all on SYS. CORE_3V3 carries only logic, the radio and the LRA.
- **The DRV2605L is on CORE_3V3, the same rail as the I2C pull-ups.** Its SDA/SCL/EN/TRIG abs max is VDD + 0.3 V, and it is never gated.
  - The checker confirms that SYS would also satisfy this, since CORE_3V3 ≤ SYS by topology.
  - CORE_3V3 is kept anyway: it avoids a SYS ripple path into the LRA drive. The LRA peak is counted against the 750 mA.

**Measurement links (0603, 0 Ω):**
- **LINK_BAT:** the whole board's battery current.
- **LINK_REG:** the regulator plus everything on CORE_3V3.
- Both are counted in the dropout audit at 50 mΩ each.

## TPS62840

**Configuration:**
- TPS62840DLCR (VSON-HR-8, DLC0008B).
- VSET 267 k 1 % (Table 1: 3.3 V; band 256.3–277.7 k).
- MODE and STOP to GND; EN to VIN.
- L: Murata DFE201612E-2R2M=P2, 2.2 µH, 116 mΩ.
- CIN 4.7 µF; COUT 10 µF (0603 10 V).

**Output capacitance:**
- CORE_3V3 carries **37.4 µF nominal** in total (from the netlist: COUT 10, module 22 + 0.1, IMU 2.2 + 0.1 + 0.01, DRV 1, two switch inputs 1 + 1).
- The limit is 3–40 µF *effective*. The nominal total is already under 40, and DC bias only lowers it: OK.

**Load and 750 mA margin** (`out/budget.md`):

| | CORE_3V3 |
|---|---|
| Average, awake (radio RX, HOME) | **102 mA** |
| Worst case, everything at once, datasheet peaks (TX 355 + LRA 150 + panel + rest) | **519 mA → 231 mA margin (31 %)** |
| Same, with the S3 at Espressif's "≥ 0.5 A supply" rule | **664 mA → 86 mA margin (11 %)** |
| High-side current limit | 1.0 A min: never reached |

**It fits.** It doesn't need the STOP-and-report path.

**100 % mode.** R_HS is 430 / 600 mΩ typ / max, **specified only at VIN = 3.6 V**. The drop below that is the low-battery floor below.

## Battery / charger

**BQ25185DLHR:**
- **ILIM/VSET 18 k:** 4.2 V, **500 mA input limit**.
- **ISET 1.0 k:** 300 mA charge, provisional; re-set to ≤ 1C once the cell is chosen.
- **TS/MR:** the cell's 10 k NTC (β 3435), or DNP 10 k to GND if the cell has none.
- **/CE:** to GND.
- **STAT1/2:** 10 k pull-ups to CORE_3V3.
- **Caps:** IN 1 µF 25 V and SYS 10 µF 25 V (TI requires 25 V ratings), BAT 1 µF.

**Charging current actually advertised:**
- With Rd only, the source advertises **Default USB power, 500 mA** (USB 2.0).
- The charger's 500 mA ILIM is inside that.
- Charge current (300 mA) plus system load share the 500 mA; the battery supplements SYS above it.

| Mode | Behaviour |
|---|---|
| USB + good battery | SYS 4.5 V regulated, charge 300 mA; STAT H/L while charging; above 500 mA total the battery supplements SYS |
| USB, no battery / dead battery | SYS powered from IN (6.1). **Only 500 mA is available**: a TX burst plus the display (≈ 420 mA) fits; audio + haptics + TX (969 mA) doesn't. **Firmware rule:** with VBAT < 3.3 V or STAT2 toggling, run a minimal "charging" mode with no audio, haptics or RGB. |
| Battery only | SYS = VBAT − I × 0.14 Ω max; BATFET off below 3.0 V (hard cut-off) |
| Ship (factory) mode | Enter: USB present, hold TS/MR low 10 s (fixture pad **TS_MR**), then remove USB. Drain 3.2 µA typ. **Exit only by applying USB** (6.3.8): no button can wake it. |
| Battery hot-plug | Inserting the cell on USB: handled by the power path. Inserting on battery alone: SYS rises through the BATFET into ~31 µF on SYS + ~37 µF on CORE_3V3, and the S3 boots (EN RC). The inrush vs the cell's protection trip is **[TEST]** |
| Dead-battery recovery | Precharge (BQ25185 internal), SYS from USB, then the rule above until VBAT ≥ 3.5 V |

**Gauge:** MAX17048G+T10, **I2C 0x36** (0x6C/0x6D, p16). VDD and CELL are on VBAT with 0.1 µF. CTG and QSTRT go to GND. ALRT is unconnected, and the firmware polls SOC.

**Found at Gate C:** SDA and SCL each have a 0.2 / 0.4 µA pull-down (IPD), which draws from the 4.7 k pull-ups in deep sleep. It's added to the budget.

## CORE_3V3 minimum-voltage audit

| CORE_3V3 | S3 + USB PHY | Flash | Panel / GC9A01 | ICM-42670-P | DRV2605L | TSOP75438 | TLV9061 | TPS22916 | RGB data |
|---|---|---|---|---|---|---|---|---|---|
| 3.2 V | OK (3.0–3.6) | OK (2.7–3.6) | OK (VCI 2.7–3.3) | OK (1.71–3.6) | OK (2.0–5.2) | OK (2.0–5.5) | OK (1.8–5.5) | OK (1.0–5.5) | OK (VIH 0.6 × SYS) |
| 3.1 V | OK | OK | OK | OK | OK, less LRA drive | OK | OK | OK | OK |
| 3.0 V | **OK, at its minimum** | OK | OK | OK | OK, less LRA drive | OK | OK | OK | OK (VIH 2.1 V at SYS 3.5 V) |

**The upper side is a waiver.** The panel VCI maximum is 3.3 V (Winstar p3), while CORE_3V3 is 3.3 V ± set-point tolerance and +30 mV entering 100 % mode.
- That can sit a few percent over the panel's operating maximum.
- It stays far under the 4.6 V absolute maximum.
- GC9A01 panels run at 3.3 V routinely.
- Winstar to confirm.

## Low-battery floor (provisional, not locked)

**Requirement:** CORE_3V3 ≥ 3.0 V at the load.

In 100 % mode:
- CORE_3V3 = VIN − I × (R_HS + DCR + LINK_REG);
- VIN = VBAT(gauge) − I_SYS × (RON_BAT + LINK_BAT).

| R | Event | I 3V3 | I SYS | VIN_REG min | **VBAT at the gauge, min** |
|---|---|---|---|---|---|
| max | worst-case event | 519 mA | 969 mA | 3.41 V | **3.59 V** |
| max | TX burst, no audio/haptics | 375 mA | 420 mA | 3.30 V | 3.38 V |
| typ | worst-case event | 519 mA | 969 mA | 3.31 V | 3.47 V |
| typ | TX burst | 375 mA | 420 mA | 3.22 V | 3.29 V |

**Provisional floor:** shut down at **VBAT(gauge, loaded) = 3.55 V**, with a low warning at 3.65 V.

This costs capacity: a LiPo still holds a few percent below 3.55 V loaded. Those figures depend on the cell, and none are claimed here.

What locks it:
1. R_HS at VIN 3.1–3.4 V, measured at bring-up (TI specifies it only at 3.6 V);
2. the chosen cell's resistance, which the gauge-side figure excludes;
3. the measured worst-case event.

The TPS63802 remains the Gate G fallback if the floor costs too much.

## Deep-sleep budget

Generated in `out/budget.md`.

**CORE_3V3 side (µA):**

| Item | Typ | Max (datasheet) | Worst used | Source |
|---|---|---|---|---|
| S3 deep sleep, ext0 + ext1 (RTC peripherals on) | 8.0 | UNKNOWN | 12 | [DS] typ only |
| ICM-42670-P wake-on-motion | 4.4 | UNKNOWN | 10 | TDK product page; not in DS v1.0 |
| DRV2605L, EN low | 4.0 | 7.0 | 7.0 | [DS] |
| ENC_A 470 k (low at half the detents) | 3.5 | 7.0 | 7.0 | [CALC], provisional |
| MAX17048 IPD on SDA/SCL | 0.4 | 0.8 | 0.8 | [DS], **new** |
| IMU INT leakage × 2 | 0 | 0.2 | 0.2 | [DS] |
| TPS22916 × 2 off | 0.02 | 0.2 | 0.2 | [DS] |
| TPD1E10B06 × 3 (encoder) | 0.03 | 0.3 | 0.3 | [DS] ≤ 100 nA |
| ENC_B, ENC_SW, straps, STAT, VBUS divider | 0 | 0 | 0 | design |
| **Subtotal** | **20.4** | | **37.5** | |

**Battery side (µA):**

| Item | Typ | Max (datasheet) | Worst used | Source |
|---|---|---|---|---|
| CORE_3V3 loads converted (typ 3.7 V / 80 %; worst 3.6 V / 70 %) | 22.7 | UNKNOWN | 49.1 | [CALC] |
| TPS62840 Iq | 0.06 | UNKNOWN | 0.12 | [DS] |
| BQ25185, battery only | 4.0 | 5.0 | 5.0 | [DS] |
| MAX17048, hibernate | 3.0 | 5.0 | 5.0 | [DS] |
| MAX98357A shutdown | 0.6 | 2.0 | 2.0 | [DS] |
| Backlight FET + LED leakage | 0.01 | 1.0 | 1.0 | [DS] IDSS |
| TPS22916 (AUX_SYS) off | 0.01 | 0.1 | 0.1 | [DS] |
| PCB / capacitor leakage margin | 1.0 | — | 3.0 | allowance |
| **Total** | **31.4** | | **65.3** | |

**Reading:**
- **31.4 µA typical, under the 35 µA target.**
- The worst stack is 65 µA. That is every maximum, the S3/IMU allowances and 70 % efficiency, all at once. It's still ≈ 53 µA even with the S3 and IMU at typical.
- **The ≤ 50 µA hard limit applies to the measured board**, and a current meter is required for it.
- The worst-case conversion uses VBAT 3.6 V. Gate B.1's 3.0 V / 70 % combination can't occur, because at 3.0 V the regulator is in 100 % mode and passes the current through.

## Active / peak budget

| | Typ | Peak | Notes |
|---|---|---|---|
| CORE_3V3 | 102 mA | 519 mA (664 mA by the S3 supply rule) | see TPS62840 |
| SYS loads besides the regulator | ~18 mA | 450 mA | backlight 30, amplifier 260 (0.77 W 8 Ω), RGB 60 (allowance), IR 100 |
| Battery | ≈ 125 mA | **969 mA** (1.11 A by the S3 rule) | the cell and protection must deliver **≥ 1.34 A peak** |

**Worst-case simultaneous event, with no "never together":** Wi-Fi TX + full backlight + full-scale audio + LRA + RGB white + IR TX + IMU + panel, all at once.
- CORE_3V3 519 mA: within 750 mA.
- SYS 969 mA: within BQ25185's 3.125 A and the connector's 2 A.
- It sets the 3.59 V (max-R) floor figure above.

## Display

**Part:** Winstar WF0128BTYAA4DNN0.
- **Pinout: confirmed** against the Winstar spec (p4, p5):
  - 1–6 NC
  - 7 VLED+
  - 8 VLED−
  - 9 GND
  - 10 CS
  - 11 SCL
  - 12 SDA
  - 13 RS
  - **14 TE**
  - 15 RESET
  - 16 VCI
  - 17 NC
  - 18 GND
- **IOVCC is not on the FPC.** It is tied to VCI on the panel (inferred).
- **VCI:** 2.7 / 2.8 / 3.3 V. **Logic levels:** VIH ≥ 0.8 × IOVCC (Winstar), stricter than the 0.7 × IOVCC in the GC9A01 datasheet.
- **Outline and FPC:**
  - outline 35.60 × 37.74 × 1.56 mm, active area 32.40 mm;
  - FPC 0.5 mm pitch, 0.30 ± 0.05 mm thick at the contacts including the stiffener;
  - stiffener 4.5 mm, contacts 3.5 mm;
  - tail 70.1 mm.

**Contact side:** the contacts are on the viewing face. With the tail folded 180° behind the panel onto the PCB, the contacts face the PCB, so the connector must be bottom-contact.

**Connector: Hirose FH12-18S-0.5SH(55).**
- Bottom contact, flip-lock, for 0.3 mm FPC, 2.0 mm high. It has a stock KiCad footprint.
- Hirose recommends 0.35 mm fingers; the panel's are 0.30 mm. **Check against the drawing at Gate D.**
- Folding mirrors pin 1. **A 1:1 paper check of pin 1 is a Gate D item.**
- The alternative is Molex 503480-1800 (dual contact, 1.0 mm high), which would work for either fold.

**Panel power:** TPS22916C.
- Slow rise, with 150 Ω quick output discharge, so the rail really goes to 0 V when off.
- LCD_3V3 carries 1 µF + 2 × 0.1 µF.

**Backlight** (the LED count, Vf, rated current and maximum are **UNKNOWN**; Winstar's short spec has no backlight table):
- **Circuit:**
  - IO8 LEDC PWM (~30 kHz) feeds a 32.4 k / 1.0 k divider, giving a 0 / 98.8 mV reference.
  - The TLV9061 drives a DMG2302UK with a 3.3 Ω sense resistor, sinking **30 mA at 100 %**.
  - That is 75 % of the sibling panel's 40 mA typical at Vf 3.0 / 3.2 / 3.4 V.
- **Supply:** from SYS. It regulates for SYS ≥ ~3.5 V, which is at the low-battery floor anyway.
- **Why not 3.3 V:** CORE_3V3 has no headroom (Vf is up to 3.4 V).
- **Why not a series resistor from SYS:** the current would swing about 3:1 across the battery range.
- **Rest level and off:** the 3 % rest level is a 1 µs pulse at 30 kHz.
- **Default off:** IO8 100 k and BL_REF 1.0 k to GND, a 100 k gate pull-down, and the op-amp unpowered with the panel. Full off is 0 V reference plus the FET off.
- **[TEST]:**
  - the sink's rise time at a 1 µs pulse (TLV9061 10 MHz);
  - loop stability (a DNP 100 pF compensation footprint is provided).
- **Rs is changed once the panel data arrives.**

## Display power sequence

**GC9A01 timings** (the GC9A01 datasheet; Winstar gives none):
- SLPIN: 5 ms before the next command, 120 ms to complete.
- SLPOUT: 5 ms.
- RESX low > 10 µs.
- IOVCC ≤ VCI (they're tied on this panel).

**Power-down (before deep sleep):**
1. Last frame done.
2. Backlight off (IO8 low: reference 0, FET off).
3. SLPIN (0x10), then wait 120 ms.
4. CS, MOSI, SCLK, DC and RST driven **low**; TE input isolated.
5. LCD_PWR_EN low. The switch's quick discharge pulls LCD_3V3 to 0 V.
6. Deep sleep. The enables can be Hi-Z, since every one has a hardware pull-down.

**Power-up:**
1. RST low (already held low by its 100 k).
2. LCD_PWR_EN high. Wait for the switch's slow rise plus settling, a few ms [TEST].
3. Release RST, then wait 120 ms. The panel-side reset delay is not in the GC9A01 datasheet, so the value is the common init practice, to be confirmed.
4. Init, SLPOUT, wait 5 ms, then the frame.
5. Backlight ramp.
6. WAKEPOP.

**Back-power audit** (`out/erc.txt`: generated from the netlist, every always-on signal that reaches a part on a gated rail):

| Gated rail | Signal | Rule when the rail is off |
|---|---|---|
| LCD_3V3 | CS, SCLK (after 22 Ω), MOSI (after 22 Ω), DC, RST | **low or Hi-Z, never high**. RST also has a 100 k pull-down |
| LCD_3V3 | LCD_BL → TLV9061 IN+ | Harmless by construction: the divider limits IN+ to ≤ 0.1 V |
| AUX_SYS | RGB_DATA → 330 Ω → SK6805 DIN | Low before AUX goes off, until after it's back on. **Waived:** ERC domain rule, "back-power, firmware-sequenced"; 330 Ω limits it to < 8 mA if the rule is ever broken |
| AUX_3V3 | IR_RX is the receiver's output, not an input to it | No path |

## Encoder / wake

**Parts:** EC11E1534408 (15 pulses / 30 detents, push). The shaft and height are fixed at Gate D.

**Wake lines:**
- A to IO2 (ext0), B to IO6, and the switch to IO1 (ext1).
- **External pulls on A and SW only: 470 k, provisional.** The 100 k / 220 k / 470 k / 1 M rig test chooses the value.
- B has no external pull: an internal pull while awake, isolated in deep sleep.
- TPD1E10B06 ESD on each line (≤ 100 nA).
- DNP 1 nF RC footprints.

**The wake API is unchanged** from Gate B.1 (ext1 ANY_LOW on IO1 | IO4, ext0 on IO2 at the opposite level).

**Hardware proof is still blocked:** it needs 50 cycles per source on an official S3 board.

## IMU

**ICM-42670-P** (DS-000451 r1.0):
- Supplies: VDD and VDDIO on CORE_3V3; VDD 0.1 µF + 2.2 µF X7R, VDDIO 10 nF X7R (Table 10).
- Interface pins: AP_AD0 to GND gives **0x68**; AP_CS to VDDIO selects I2C; FSYNC to GND; RESV pins unconnected.
- **INT1 to IO4 (wake), INT2 to IO5:** each with a 100 k pull-up. INT_CONFIG resets to open-drain, active-low, pulsed, and the firmware sets latched mode.
- The wake-on-motion current is still **UNKNOWN** from the datasheet.

## Haptics

**DRV2605LDGSR** (VSSOP-10), on CORE_3V3:
- Decoupling: REG 1 µF, VDD 1 µF (Table 32).
- Enable and trigger: EN to IO41 and IN/TRIG to IO42, each with a 100 k pull-down (plus the internal 2 M on EN).
- I2C: **0x5A**.
- LRA leads: to a JST SH 2-pin connector. **The LRA itself (8–10 mm) is chosen at Gate D**, and its current is allowed at 150 mA peak.

## Audio

**MAX98357AETE+T** (TQFN-16), on SYS:
- VDD 0.1 µF + 10 µF.
- **SD_MODE driven directly from IO38 (AMP_SD):** high selects the left channel, low is shutdown. The internal 100 k keeps it off through reset.
  - The abs-max rule (VDD + 0.3 V) holds because CORE_3V3 ≤ SYS.
- **GAIN_SLOT open = 9 dB.** A DNP 0 Ω to GND gives 12 dB.
- EMI options on OUTP/OUTN: 0 Ω (ferrite option) and DNP 1 nF.
- Speaker: 8 Ω on a JST SH 2-pin; the speaker is chosen at Gate D. 0.77 W at 3.7 V (1 % THD).

**Sequence** (MAX98357A: 7 ms turn-on):
1. I2S clocks running.
2. AMP_SD high.
3. After ≥ 7 ms, audio.
4. The reverse on shutdown.

This removes M4.1's uncontrolled amplifier state.

## IR / RGB

**IR TX:**
- VSMB2943GX01 (940 nm, 100 mA continuous) from SYS through 33 Ω (0805).
- Current: 98 mA at SYS 4.6 V, ≈ 65 mA at 3.5 V.
- Low-side DMG2302UK: gate 100 Ω, with 100 k pull-downs on IR_TX and on the gate. The S3 never drives the LED directly.

**IR RX:** TSOP75438 (38 kHz, VS 2.0–5.5 V, 0.35 mA) on **AUX_3V3** through 100 Ω / 0.1 µF.

**RGB:** SK6805-EC20.
- **VDD 3.5–5.5 V: no addressable RGB LED checked is rated below 3.5 V.** It's on AUX_SYS.
- **Firmware must inhibit it below SYS 3.6 V.**
- Data: IO33 through 330 Ω; VIH 0.6 × VDD is 2.76 V at 4.6 V, so 3.3 V data has margin.
- The XL-1615RGBC-WS2812B has a lower VIH (0.55 × VDD), but it is "100 % functional" only at 4.5–5.5 V, so it was not chosen.

**Aux gate audit:**
- One GPIO (IO40) drives both switches. Each switch has its own 750 k pull-down, so both stay off through reset.
- Both are off in deep sleep: 0 µA plus switch leakage.

## USB-C

**Parts:**
- Receptacle: GCT USB4105-GF-A (16-pin, top mount).
- CC1 and CC2: 5.1 k each to GND.
- D±: TPD2E2U06DRLR (1.5 pF), with 22 Ω series at the module.
- VBUS: TPD1E10B06, clamping at 10 V at 1 A, under the charger's 25 V IN abs max.
- SBU: unconnected. Shield: to GND.

**Routing:** D+/D− as a **90 Ω differential pair** from the connector through the ESD to the module. The ESD sits at the connector and the series resistors at the module. There are no stubs; the two receptacle rows join at the pads.

**VBUS_SENSE:** 100 k / 150 k, so 5.5 V → 3.30 V at IO35.

**Advertised current:** Default, 500 mA (see Battery / charger).

## I2C

**Bus:** IO47 SDA / IO48 SCL at 3.3 V, with **one pair of 4.7 k pull-ups to CORE_3V3** (the checker verifies exactly one each), at 400 kHz. The MAX17048 limits it to 400 kHz.

| Device | Address | Power domain | Note |
|---|---|---|---|
| MAX17048 | 0x36 | VBAT (always on) | I/O rated −0.3 to 5.5 V; IPD 0.2–0.4 µA per line |
| DRV2605L | 0x5A | CORE_3V3 (never gated) | pins rated VDD + 0.3 V |
| ICM-42670-P | 0x68 | CORE_3V3 | VIH 0.7 × VDDIO |

The three addresses are unique, checked by the ERC.

**No device on the bus is ever unpowered while the bus is pulled up.** That's the reason the DRV2605L isn't gated.

## ODD BUS impact

**None.** There are no changes to the protocol, security, radio power (the ~8.5 dBm cap stays) or pairing. The TX cap is not credited in the power budget, which uses the datasheet's 355 mA.

## ERC

`out/erc.txt`, 142 parts / 77 nets. **0 open errors.**

| Category | Result |
|---|---|
| SKiDL ERC (pin-type conflicts, drive, unconnected pins) | 0 errors. 4 warnings, all waived |
| Connectivity: every non-NC pin connected, no single-pin nets | pass |
| Domain / abs max: every limited pin vs its supply's minimum + 0.3 V, gated rails modelled at 0 V | pass; 1 waived |
| Capacitor rating vs rail maximum (and < 1.5 × for class II) | pass |
| Hardware default-off (LCD_PWR_EN, LCD_BL, AMP_SD, HAPTIC_EN, HAPTIC_TRIG, IR_TX, AUX_PWR_EN) | pass: each has an external or internal pull-down, and none a pull-up |
| I2C: one pull-up per line, unique addresses | pass |
| Footprint exists, and every pin has a pad | pass (see the footprint audit) |

**Waivers, by category:**
1. **Open-drain to MCU input** (4): CHG_STAT1, CHG_STAT2, IMU_INT1 and IMU_INT2 connect an open-collector pin to a bidirectional S3 pin. The S3 pin is configured as an input, and the net has its pull-up.
2. **Back-power, firmware-sequenced** (1): the RGB DIN on AUX_SYS, as above.
3. **Vendor limit at the nominal edge** (report only): panel VCI max 3.3 V vs CORE_3V3.

**The checker was tested against planted faults:**
- a 4 V capacitor on SYS: caught;
- a removed IR_TX pull-down: caught.

Two planted cases correctly passed, because they are legal:
- the DRV2605L on SYS (CORE_3V3 ≤ SYS by topology);
- HAPTIC_EN without its external pull-down (it still has the internal 2 M).

## Footprint audit

Each footprint was checked pad by pad against the vendor's land pattern or package drawing:
pitch, pad size, row span, exposed pad (EP), pin 1 and numbering.

The ERC also checks that every pin has a pad.

**The eight brief parts:**

| Part | Footprint used | Verdict | Source |
|---|---|---|---|
| ESP32-S3-MINI-1-N8 | `RF_Module:ESP32-S2-MINI-1` | **MATCH** (see below) | MINI-1 v1.7 Fig 11-1 |
| BQ25185 | `Package_DFN_QFN:Texas_DLH0010A_WSON-10-1EP_2.2x2mm_P0.4mm_EP0.9x1.5mm` | **MATCH** (see below) | SLUSF65B DLH0010A |
| TPS62840 | **custom** `MAO_RevA:TI_DLC0008B_VSON-HR-8_1.5x2mm_P0.5mm` | KiCad's VSON-HR-8 was a **MISMATCH**, so a custom footprint replaces it (see below) | SLVSEC6D, 4224310/A |
| ICM-42670-P | `Package_LGA:LGA-14_3x2.5mm_P0.5mm_LayoutBorder3x4y` | **ACCEPTABLE** (see below) | DS-000451 r1.0 p18, p37 |
| DRV2605L | `Package_SO:MSOP-10_3x3mm_P0.5mm` | **ACCEPTABLE** (see below) | SLOS854D p71, DGS0010A |
| MAX98357A | `Package_DFN_QFN:TQFN-16-1EP_3x3mm_P0.5mm_EP1.23x1.23mm` | **ACCEPTABLE, land pattern UNVERIFIED** (see below) | datasheet 19-6779 Rev 7 pp35–37 |
| MAX17048 | `Package_DFN_QFN:TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm` | **UNVERIFIED** (see below) | datasheet p18 |
| TPS22916C | `Package_BGA:Texas_PicoStar_BGA-4_0.758x0.758mm_Layout2x2_P0.4mm` | **MATCH** (see below) | SLVSDO5F YFP0004 |

- **ESP32-S3-MINI-1:**
  - Edge pads: 60 × 0.4 × 0.8 mm at 0.85 mm pitch, side spans 11.9 mm, corner-pad spans 14 mm.
  - Corner pads: 4 × 0.8 × 0.8 mm.
  - EPAD: 3 × 3 of 1.2 mm, 4.5 mm overall, centred 7.7 mm from the bottom edge.
  - Numbering: the edge pads number the same as the S3 symbol. The corner and EPAD pads are all GND.
  - KiCad has no S3-MINI-1 footprint; this one's description cites the S3-MINI-1 datasheet, and it carries an antenna keep-out zone.
- **BQ25185:** built for this part by name. Pads 0.75 × 0.2 at a 2.2 mm span cover the 0.25–0.35 mm leads. The EP is 0.9 × 1.5 nominal.
- **TPS62840 (custom footprint):**
  - KiCad's `Texas_VSON-HR-8` was drawn for the TPS62823. Its pads are 0.8 mm long at a 1.45 mm span.
  - TI's DLC0008B land example: 8 × 0.25 × 0.6 mm at a (1.3) mm span, no EP. The custom footprint follows it exactly.
- **ICM-42670-P:**
  - No land pattern is published.
  - Pads are 0.625 × 0.35 over 0.475 × 0.25 leads, about +0.05 mm per side.
  - The pin 1 corner and 4/3 pin sides match Fig 4 / Table 9.
- **DRV2605L:** TI pads are 1.45 × 0.3 at a 4.4 mm span; KiCad's are 1.5 × 0.35 at 4.2 mm, leaving 0.15 mm between pads instead of 0.2.
- **MAX98357A:**
  - KiCad drew it from T1633-5 / land 90-0032.
  - T1633+4 has EP 0.95–1.25 mm; KiCad's 1.23 mm is inside that.
  - Land pattern 90-0031 could not be fetched.
- **MAX17048:** the EP of 0.8 × 1.2 is only KiCad's claim. Outline 21-0168 and land 90-0065 could not be fetched (analog.com times out).
- **TPS22916C:** 4 × Ø0.23 mm non-solder-mask-defined pads at 0.4 mm pitch, identical to the YFP0004 land example. The body outline is 0.758 vs 0.78 mm (cosmetic only).

**The other parts:**

| Part | Footprint | Verdict |
|---|---|---|
| USB4105-GF-A | `Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal` | **MATCH** (GCT drawing Rev B4, recommended PCB layout). Pads A1…B12 + SH. |
| FH12-18S-0.5SH(55) | `Connector_FFC-FPC:Hirose_FH12-18S-0.5SH_1x18-1MP_P0.50mm_Horizontal` | **MATCH** (Hirose EDC3-150229-11): 0.3 × 1.3 mm pads at 0.5 mm pitch, 2 × MP |
| TPD2E2U06DRLR | `Package_TO_SOT_SMD:SOT-553` | **ACCEPTABLE**: DRL is the 5-pin SOT. Pins 1 NC, 2 NC, 3 IO1, 4 GND, 5 IO2 |
| TPD1E10B06DPYR | `Package_SON:Texas_DPY0002A_0.6x1mm_P0.65mm` | **MATCH** |
| DMG2302UK | `Package_TO_SOT_SMD:SOT-23` | **ACCEPTABLE** (KiCad uses IPC pads, longer than Diodes' 0.8 × 0.9). Pins 1 G, 2 S, 3 D |
| DFE201612E-2R2M | `Inductor_SMD:L_Murata_DFE201610P` | **MATCH**: Murata's DFE201612E land is the same (0.55 × 1.6 mm, 0.9 mm gap) |
| SK6805-EC20 | **custom** `MAO_RevA:LED_SK6805-EC20_2.0x2.0mm` | KiCad's WS2812B-2020 is a **MISMATCH** (see below) |
| VSMB2943GX01 | **custom** `MAO_RevA:Vishay_VSMB2943_GullWing` | Vishay 83486 Rev 1.7 p5: 1.35 × 0.75 mm pads at ±1.9 mm (see below) |
| TSOP75438 | **custom** `MAO_RevA:Vishay_TSOP75xxx_Heimdall_SMD` | Vishay 82494 Rev 2.4 p7: 4 × 0.8 × 1.8 mm pads at 1.27 mm pitch; pins 1 GND, 2 VS, 3 OUT, 4 GND (see below) |
| EC11E, JST PH / SH, passives | KiCad library | stock footprints |

- **SK6805-EC20:**
  - KiCad's WS2812B-2020 pads sit 0.27 mm too far out.
  - The EC20 pinout is also different: **1 GND, 2 DIN, 3 VDD, 4 DOUT** (spec SK6805-EC20-001 Rev A1 p4).
  - The custom footprint follows the EC20 spec's recommended pads (p5), and the schematic symbol now uses the EC20 pinout.
- **VSMB2943GX01:** the datasheet gives no pin numbers. Pad 1 = cathode (the pin-ID side), with a silk bar.
- **TSOP75438:** the drawing gives no pad-to-body dimension, so the lens position relative to the pads is **estimated**. Check it against the 3D model before placing the part at the board edge.

The custom footprints are generated by `schematic/make_footprints.py` and `make_footprints_b.py`. `kicad-cli fp upgrade` loads the library without errors.

**Still unverified:**
- the MAX98357A land pattern;
- the MAX17048 EP size;
- the TSOP lens offset.

These are **Gate D blockers**, needed before the footprint freeze.

## Unresolved items

1. **Panel electrical data:**
   - backlight LED count, Vf, rated and maximum current;
   - VCI supply and sleep currents;
   - reset timing;
   - whether TE is push-pull.
   Winstar's full spec is needed; Rs (backlight) waits on it.
2. **Wake-on-motion current** of the ICM-42670-P (datasheet v1.2 or a measurement).
3. **TPS62840 R_HS at VIN 3.1–3.4 V** (it sets the low-battery floor) [TEST].
4. **Low-battery floor:** 3.55 V (gauge, loaded) is provisional.
5. **Encoder pull value**, from the rig test.
6. **Backlight sink:** 1 µs-pulse fidelity and loop stability [TEST].
7. **The LRA and the speaker** (Gate D), and so the 150 mA LRA allowance.
8. **Battery:** capacity, discharge rating ≥ 1.34 A peak, protection, NTC, and the ISET value.
9. **The panel's upper VCI tolerance** (Winstar to confirm 3.3 V + tolerance).
10. **Drawn KiCad schematic sheets**, generated from the netlist for human review at Gate D.
11. **Land patterns still unverified:** MAX98357A (90-0031), the MAX17048 EP (21-0168 / 90-0065), and the TSOP75438 lens offset. See the footprint audit.
12. **`components/odd_field/`:** the question of deleting it (from M4.1) is still open.

## Gate D blockers

- **The enclosure.** Board outline, display stack, fold direction and FPC length, encoder height, speaker and LRA volume, antenna keep-out at the board edge.
- **A physical S3 wake test:** press, turn and IMU, 50 cycles per source, on an ESP32-S3-DevKitC-1-N8R8 or another official board.
- **The pull test**, 100 k / 220 k / 470 k / 1 M.
- **A panel sample in hand**, with the pin-1 paper check on the FH12.
- **A battery chosen.**
- **A current meter.** It's needed for the ≤ 50 µA proof and for the floor.
- **Footprint freeze:** the MAX98357A land pattern, the MAX17048 EP and the TSOP lens offset are verified.
- **Gate E complete** before the order.
