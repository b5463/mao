# MAO M5.0 — Gate B.1 closure (schematic-ready specification)

- **Branch:** `feat/mao-m5-rev-a`. The Gate B review is at `17ba1fa`.
- **Scope:** close the electrical ambiguities before Gate C. There is no
  schematic, placement or layout yet.
- **Markings:**
  - **[DS]** = verified against the primary datasheet or Espressif
    document named;
  - **[OPEN]** = explicitly unresolved (no Gate B "verify" was carried over
    as a fact);
  - **[TEST]** = needs a bench test.

Primary sources used in this pass:
- ESP32-S3-MINI-1/1U datasheet v1.7;
- ESP-IDF v6.0.3 source (`esp_sleep.h`, `soc/esp32s3/include/soc/soc_caps.h`);
- TI BQ25185 (SLUSF65B), TPS63802 (SLVSEU9D), TPS62840, TPS63900,
  DRV2605L (SLOS854D) and TPS22916;
- Analog/Maxim MAX98357A/B and MAX17048/49;
- TDK ICM-42670-P DS-000451 v1.0.

## 1. Audio GPIO audit (§2)

The Gate B map **already allocated the full I2S interface**, not one PDM
pin:

| Signal | GPIO |
|---|---|
| BCLK | IO17 |
| LRCLK/WS | IO18 |
| DIN | IO21 |
| SD_MODE | IO38 |

No correction was needed. The table in §5 carries them.

## 2. Amplifier decision (§3–4, §19)

**Option A: standard I2S, MAX98357A.** Recommended.

Verified from the datasheet [DS]:

| Parameter | Value |
|---|---|
| Supply | 2.5–5.5 V (UVLO 1.4–2.3 V) |
| Quiescent | 2.4 mA typ / 2.85 mA max at 3.7 V (2.75 / 3.35 mA at 5 V) |
| **Shutdown** (SD_MODE = 0 V) | **0.6 µA typ / 2 µA max** |
| Standby (SD_MODE high, no BCLK) | 340 / 400 µA, so never park it that way |
| SD_MODE pull-down | internal, 100 kΩ (92–108): the amplifier is **off by default** through reset, boot and deep sleep |
| SD_MODE thresholds | shutdown below ≈ 0.16 V. Driven straight from a 3.3 V GPIO it sits above the 1.4 V threshold and **plays the left channel** (MAX98357A). MAO is mono: firmware puts the stream on left |
| Interface | I2S / left-justified 16/24/32-bit, or TDM. **BCLK must be 32, 48 or 64 × LRCLK. No MCLK.** BCLK 0.2432–25.8 MHz. VIH 1.3 V, so 3.3 V logic is fine |
| Sample rate | 8–96 kHz (M4.1 uses 16 kHz) |
| Turn-on time | 7 ms typ / 7.5 ms max |
| Click-and-pop | −72 dBV into shutdown, −66 dBV out of shutdown (A-weighted) |
| Output | filterless class D, 3.2 W into 4 Ω at 5 V, 92 % efficient at 1 W into 8 Ω |
| Packages | WLP-9 (1.345 × 1.435 mm) or TQFN-16 (3 × 3 mm) |

**Supply:** from **SYS (VBAT or 4.5 V)**, not the 3.3 V rail. That keeps the
speaker's current off the regulator. In deep sleep it costs only its shutdown
current, on the battery side.

**Sequence:**
1. I2S running with zero samples.
2. SD_MODE high.
3. Wait at least 7.5 ms.
4. Play.

Shutdown is the reverse: SD_MODE low while the zero stream is still running.
The PDM floor and its LEDC glide from M4.1 aren't needed on Rev A. Firmware
decides whether the amplifier stays enabled while MAO is awake (2.4 mA) or
is enabled per cue (7 ms latency). That's for Gate G.

**Option B: a PDM or 1-wire amplifier.** Rejected. It saves 2 pins the S3
doesn't need, and brings back the DC-floor behaviour that caused M4.1's
clicks.

**Successor comparison: [OPEN].** The MAX98360A family couldn't be retrieved:
Analog Devices' document server didn't respond from this environment. The
decision doesn't depend on it, since the MAX98357A meets every requirement.

## 3. GPIO26 and the IO33–37 caveat (§7)

**[DS] Footnote b:** *"For modules with part numbers ending with -N4R2, IO26
connects to the embedded PSRAM and is not available for other uses."*
- IO26 is usable **on ESP32-S3-MINI-1-N8 only**.
- The board definition is tied to that exact part number. It isn't valid for
  the N4R2 or any PSRAM variant.

**[DS] Footnote to table 3-1:** *"For pin 28–29, 31–33 the default function
is decided by eFuse bit."* Those are **IO33–IO37**.
- Their reset-state function isn't guaranteed to be GPIO.
- **Rule:** no signal whose state at reset matters (a gate that must stay
  low) goes on IO33–37. Only inputs, or loads that are powered off at reset,
  go there.
- **Changes from Gate B:** IR_TX and HAPTIC_EN/TRIG moved off those pins.

## 4. Straps (§8)

GPIO0, GPIO3, GPIO45 and GPIO46 carry no product signal:
- **GPIO0:** the BOOT fixture pad only.
- **GPIO3:** unused.
- **GPIO45:** unused (the VDD_SPI voltage strap).
- **GPIO46:** unused (boot / ROM print).

## 5. Final GPIO map (§5)

| GPIO | Signal | Dir | RTC / wake | Reset / default | Pull | In deep sleep |
|---|---|---|---|---|---|---|
| 0 | BOOT pad | — | RTC (strap) | weak pull-up (strap 1 = SPI boot) | ext 10 k up | — |
| 1 | ENC_SW | in | **ext1 ANY_LOW** | input | ext high-value up (§7) | armed |
| 2 | ENC_A | in | **ext0 (level)** | input | ext high-value up (§7) | armed at the opposite of its level |
| 3 | — (strap) | — | — | floating | — | — |
| 4 | IMU_INT1 | in | **ext1 ANY_LOW** | input | ext 100 k up; IMU open-drain, latched | armed |
| 5 | IMU_INT2 | in | RTC | input | ext 100 k up | — |
| 6 | ENC_B | in | RTC | input | internal pull while awake | isolated, pulls off: 0 µA even when it rests low |
| 7 | LCD_PWR_EN | out | RTC | Hi-Z | the TPS22916's own 750 kΩ pull-down | low (panel unpowered) |
| 8 | LCD_BL | out, LEDC | RTC | Hi-Z | ext 100 k down on the MOSFET gate | low |
| 9 | LCD_TE | in | RTC | input | — | input, isolated |
| 10 | LCD_CS | out | — | Hi-Z | — | low (§11) |
| 11 | LCD_MOSI | out | — | — | — | low |
| 12 | LCD_SCLK | out | — | — | — | low |
| 13 | LCD_DC | out | — | — | — | low |
| 14 | LCD_RST | out | — | Hi-Z | ext 100 k down | low |
| 15 | BOARD_ID | in | RTC | input | **tri-state strap** (to GND, to 3V3, or open) | isolated: 0 µA |
| 16 | CHG_STAT2 | in | RTC | input | ext 10 k up | high-Z on battery: 0 µA |
| 17 | I2S_BCLK | out | — | — | — | low |
| 18 | I2S_WS | out | — | — | — | low |
| 19 / 20 | USB D− / D+ | — | — | — | — | — |
| 21 | I2S_DIN | out | — | — | — | low |
| 26 | *spare* (N8 only) | — | — | — | — | — |
| 33 | RGB_DATA | out, RMT | — | eFuse-default (§3). Harmless: RGB unpowered at reset | — | low |
| 34 | IR_RX | in, RMT | — | eFuse-default. Input, receiver unpowered | — | — |
| 35 | VBUS_SENSE | in | — | eFuse-default. Input | divider from VBUS (§12) | 0 µA without USB |
| 36 | CHG_STAT1 | in | — | eFuse-default. Input | ext 10 k up | high-Z on battery |
| 37 | *spare* | — | — | eFuse-default | — | — |
| 38 | AMP_SD | out | — | Hi-Z | the amplifier's internal 100 kΩ down | low |
| 39 | IR_TX | out, RMT | — | Hi-Z | ext 100 k down on the transistor gate | low |
| 40 | AUX_PWR_EN | out | — | Hi-Z | the TPS22916's pull-down | low: RGB + IR receiver unpowered |
| 41 | HAPTIC_EN | out | — | Hi-Z | the DRV2605L's internal 2 MΩ down + ext 100 k down | low |
| 42 | HAPTIC_TRIG | out | — | Hi-Z | ext 100 k down | low |
| 43 / 44 | *spare* (UART0 TX/RX) | — | — | — | — | factory log pads |
| 45 / 46 | — (straps) | — | — | — | — | — |
| 47 | I2C_SDA | io | — | — | ext 4.7 k up to 3V3 | idle high: 0 µA |
| 48 | I2C_SCL | out | — | — | ext 4.7 k up to 3V3 | idle high |

Notes:
- **Fuel gauge ALRT:** no GPIO. SOC is polled at a low rate; nothing needs
  an interrupt.
- **DRV2605L:** it has no INT pin.
- **Haptic control:** EN and TRIG are direct GPIOs, as the brief asks.
- **IO47/48 [OPEN]:** they're in the chip's VDD_SPI domain. With this
  module's quad 3.3 V flash that should be 3.3 V, but it needs confirming
  from the module's VDD_SPI section before the schematic.

## 6. Spare GPIOs (§6)

**4 genuinely usable spares: IO26 (N8 only), IO37, IO43, IO44.**
- IO43/44 are UART0. Debugging and flashing use USB-Serial-JTAG, so they're
  free, and they double as factory log pads.
- **IO5** (IMU_INT2) becomes a fifth spare if INT2 is dropped.
- To get there, the RGB and IR receiver share one gated "aux" rail (AUX_PWR_EN,
  one TPS22916). Both are off together in deep sleep and on together when
  awake.

## 7. Wake architecture (§9–11) — API proven, hardware test pending

**[DS] ESP-IDF v6.0.3, ESP32-S3:**
- `SOC_PM_SUPPORT_EXT0_WAKEUP = 1` and `SOC_PM_SUPPORT_EXT1_WAKEUP = 1`;
- `SOC_RTCIO_PIN_COUNT = 22`;
- `SOC_RTCIO_HOLD_SUPPORTED = 1`;
- `SOC_PM_SUPPORT_RTC_PERIPH_PD = 1`;
- `ESP_EXT1_WAKEUP_ANY_LOW` / `ANY_HIGH` exist. `ALL_LOW` is ESP32-only and
  deprecated.

**The calls:**
- `esp_sleep_enable_ext1_wakeup_io((1ULL << 1) | (1ULL << 4), ESP_EXT1_WAKEUP_ANY_LOW)`
  for the press and the IMU;
- `esp_sleep_enable_ext0_wakeup(GPIO_NUM_2, !level_now)` for a turn;
- on wake, `esp_sleep_get_wakeup_causes()` and
  `esp_sleep_get_ext1_wakeup_status()` tell the press from the IMU, and
  `rtc_gpio_deinit()` returns each pin to plain GPIO.

**Power domain:** ext0 keeps the RTC peripherals powered, which the datasheet
puts at 8 µA vs 7 µA [DS].

**Re-sleep loop guard:** before sleeping, wait until neither ext1 line is
low. A held press or an unread IMU interrupt would wake it at once.

**The test firmware exists and builds for the ESP32-S3 on IDF 6.0.3:**
`tests/hw/s3_wake/`. It configures exactly this combination, prints the wake
cause and which ext1 pin fired, keeps a wake counter in RTC memory, and runs
the release guard.

**[TEST] Blocked:** there is no ESP32-S3 board on the bench. It needs any
ESP32-S3 dev board (ideally with the MINI-1-N8), three switches and the pull
resistors below. Gate C's wake proof is this run.

## 8. Encoder sleep pulls (§12–14)

**[DS] The prototype's EC11** (board file): 30 detents per revolution, 2
transitions per detent, resting at **AB = 00 or 11**, alternating each
detent.

So:
- **A rests low after half of all detents**, and B rests low with it.
- A turn always changes A within the first half-step, which suits the ext0
  wake armed at the opposite level.

**Worst-case pull current, per line at 3.3 V:**

| Pull | Current |
|---|---|
| 10 k | 330 µA |
| 100 k | 33 µA |
| 220 k | 15 µA |
| 330 k | 10 µA |
| 470 k | 7.0 µA |
| 1 M | 3.3 µA |

**Proposed scheme, the dual-strength strategy of §13:**
- **ENC_A:** an external high-value pull-up, the only one that can draw in
  sleep. The S3's internal pull-up adds strength while MAO is awake and is
  switched off before deep sleep.
- **ENC_B:** no external pull. Internal pull while awake; **isolated** in
  deep sleep, so it draws 0 µA even when it rests low. It isn't a wake source.
- **ENC_SW:** an external high-value pull-up. It only draws while the knob
  is held down.

**[TEST] The value isn't locked.** Test 100 k, 220 k, 470 k and 1 M on the
S3 test rig for:
- sleep current;
- false wakes (bounce, and EMI from backlight PWM and LRA pulses nearby);
- edge time with a 100 pF–1 nF footprint;
- awake decoding at fast spins, with the internal pull on.

**Provisional for the budget:** 470 kΩ, worst case 7 µA.

## 9. IMU: ICM-42670-P (§15–16)

**Verified [DS]:**
- **Supply:** VDD and VDDIO each 1.71–3.6 V. Both run from 3.3 V.
- **Interface:** I2C up to 1 MHz (also I3C 12.5 MHz and SPI 24 MHz).
- **Address:** 0x68 with AP_AD0 = 0, or 0x69 with AP_AD0 = 1.
- **INT1 / INT2:** each push-pull or **open-drain**, active high or **active
  low**, level or pulse mode. Open-drain leakage is 100 nA.
- **Pulse width:** 8 or 100 µs in pulse mode, so the design uses **latched
  level mode**. The S3's wake needs a level held for 3 RTC slow-clock cycles.
- **Currents:** low-noise 6-axis 0.55 mA, accelerometer 0.20 mA, gyroscope
  0.42 mA, full-chip sleep 3.5 µA (25 °C).
- **FIFO:** up to 2.25 KB.
- **Start-up:** 1 ms to register access after power-up.
- **Decoupling (application schematic and BOM):** VDD 0.1 µF + 2.2 µF X7R,
  VDDIO 10 nF X7R.
- **Package:** LGA-14, 2.5 × 3.0 × 0.76 mm.

**[OPEN] Wake-on-motion current.** The v1.0 datasheet's electrical table
lists no low-power accelerometer current. The 4.4 µA at 1.56 Hz is from
TDK's product page. It's needed from datasheet v1.2, or measured on the
rig.

**Decision:** **locked for interface and electrical design.** The
wake-on-motion current stays open in the budget below, using 4.4 µA typical
and a 10 µA allowance.

**States across boot, reset, active and deep sleep:** INT is open-drain, so
with the external 100 k pull-up it idles high. Before it's configured, and
while the IMU is powering up, it isn't driven low [DS: open-drain,
configurable polarity]. There's no false wake, and no pull current while
it's idle.

## 10. Haptics: DRV2605L (§17–18)

**Verified [DS]:**
- **Supply:** 2–5.2 V (absolute maximum 5.5 V).
- **Currents:** shutdown **4 µA typ / 7 µA max** (EN = 0 V); standby 4.1 /
  7 µA; idle 0.5 / 0.65 mA.
- **EN:** internal 2 MΩ pull-down.
- **Logic:** VIH 1.3 V, VIL 0.5 V on EN, IN/TRIG, SDA and SCL.
- **LRA:** 125–300 Hz, with automatic resonance tracking.
- **Start-up:** 0.7 ms from GO or trigger; 1.5 ms from EN high (PWM or
  analogue modes).
- **Decoupling:** 1 µF on VDD is required; the REG capacitor comes from the
  datasheet's recommended-components table.
- **Packages:** DSBGA-9 or VSSOP-10.

**Decision: option A, powered with EN low.**
- **Why supply gating is ruled out:** its absolute maximum on SDA, SCL, EN
  and IN/TRIG is **VDD + 0.3 V**. Gating its supply while the shared I2C bus
  stays pulled up to 3.3 V would back-power it through those pins, which
  violates that rating.
- **What gating would take:** a separate, gated I2C bus. That costs 2 GPIOs
  and breaks the ≥ 4 spares target.
- **Cost of option A:** 4 µA typical, 7 µA max in deep sleep.
- **Supply:** the DRV2605L runs from **3V3**, not SYS. At VBAT = 3.0 V, a
  3.3 V bus would sit at the absolute-maximum edge.

## 11. Display power-down and back-powering (§28–29)

**Rule:** while the panel's supply is off, every S3 line to the panel is
driven **low or isolated**, never high. That covers CS, MOSI, SCLK, DC and
RST (TE is an input). A line held high would power the panel through its
protection diodes.

**Power-down sequence:**
1. The last frame is finished.
2. Backlight off (IO8 low).
3. SLPIN (0x10) and at least 5 ms.
4. All panel lines low, TE isolated.
5. LCD_PWR_EN low.
6. Deep sleep.

**Wake sequence:**
1. LCD_PWR_EN high.
2. Wait for the switch's rise time plus the panel's own settling time.
3. RST pulse.
4. Panel init.
5. The frame is restored.
6. Backlight up.
7. WAKEPOP.

The timings are to be refined from the chosen panel's datasheet [OPEN].

## 12. USB host detection (§33)

- **USB data host:** USB-Serial-JTAG sees the host's SOF packets
  (`usb_serial_jtag_is_connected()`), as on M4.1. That needs no hardware.
- **Charge-only supply:** VBUS_SENSE (IO35, through a divider sized for
  3.3 V) reads VBUS present with no SOF.
- **Policy:** host present, MAO stays awake. Charge-only, the normal sleep
  policy applies. Neither, battery.
- **Cost:** the divider draws only when VBUS is present, so 0 µA on battery.

## 13. Power: regulator proposal (§20–22) — owner decision needed

The deep-sleep budget below shows the **approved TPS63802 can't meet the
35 µA design target**:
- its 11 µA is **typical only**, at 3.6 V in and 3.3 V out, not switching,
  with no maximum given [DS];
- every 3.3 V load also passes through its light-load efficiency.

| | TPS63802 (approved) | **TPS62840** (proposed) | TPS63900 |
|---|---|---|---|
| Topology | buck-boost | buck with 100 % mode | buck-boost |
| Quiescent | 11 µA typ | **60 nA; 120 nA in 100 % mode** | 75 nA |
| Output | 2 A for VIN ≥ 2.3 V | **750 mA**; high-side limit 1.0–1.4 A | > 400 mA; 1.45 A peak switch; stackable |
| Light-load efficiency | not specified at µA loads | **80 % at 1 µA out** (3.6 → 1.8 V) | > 90 % at 10 µA |
| Shutdown | 45 nA typ / 600 nA max | 25 nA | 60 nA |
| RF | — | DCS-Control, "RF friendly" | — |

**Proposed: TPS62840 for the 3.3 V rail.**

What it gains:
- about 15 µA typical in deep sleep;
- one small part.

What it costs:
- it's a buck, so below VBAT ≈ 3.35 V the rail follows the battery (100 %
  mode, a drop of roughly I × 0.2–0.27 Ω).
- The S3 runs from 3.0 V, so MAO keeps working down to about 3.1–3.2 V under
  load. The capacity lost below that is small for a LiPo, whose charge sits
  mostly above 3.5 V (the exact figure depends on the chosen cell) [OPEN].

What that forces:
- **the amplifier and the RGB LED run from SYS**, so the 3.3 V rail peak
  stays within 750 mA (see §15).

The TPS63900 is rejected on its own (400 mA is below the 355 mA RF peak plus
margin). A stacked pair is a valid but more complex alternative. The TPS63802
remains the fallback if the TPS62840 fails the RF transient test at Gate G.

**This changes an approved Gate B item, so it needs your decision.**

## 14. Charger, fuel gauge and NTC (§23–25)

**BQ25185 [DS]:**
- **Battery-only quiescent:** 4 µA typ / 5 µA max (VBAT 3.6 V, 0–85 °C).
- **Factory (ship) mode:** 3.2 / 5 µA. It's entered with a long press on
  TS/MR, and SYS stays off until IN is applied.
- **SYS:** regulated at 4.5 V with IN present, up to 3.125 A of system
  discharge.
- **Input:** 3–18 V operating, 25 V tolerant. IN needs at least 1 µF; SYS
  needs at least 10 µF (25 V rated, per TI).

**Programming values (provisional values marked):**

| Setting | Value | Result |
|---|---|---|
| ILIM/VSET | **18 kΩ** | 4.2 V battery regulation, 500 mA input limit (datasheet worked example) |
| ISET | **1.0 kΩ** (provisional) | 300 mA charge, from ICHG = 300 AΩ / RISET (285–315). Re-set to ≤ 1C once the cell is chosen; for example 600 Ω gives 500 mA |
| TS/MR | the cell's **10 kΩ NTC, β(25/85) = 3435 K** | the NTC the IC is designed for. **If the cell has none: 10 kΩ from TS/MR to GND**, per the datasheet. The design strongly prefers a cell with an NTC |
| CE | to GND | charging enabled |

**Status pins:** STAT1 and STAT2 are open-drain, with 1–20 k pull-ups.
- **Both HIGH (high-Z):** complete, sleep or disabled. Battery-only is in
  this state, so the pull-ups draw 0 µA.
- **STAT1 HIGH, STAT2 LOW:** charging.
- **STAT1 LOW, STAT2 HIGH:** recoverable fault.
- **Both LOW:** latched fault.

**MAX17048 [DS]:**

| Mode | Typ | Max |
|---|---|---|
| Sleep (≤ 50 °C) | 0.5 µA | 2 µA |
| Hibernate (enters and exits automatically) | 3 µA | 5 µA |
| Active | 23 µA | 40 µA |

- **Other facts:** I2C up to 400 kHz; I/O pins rated −0.3 to +5.5 V; ALRT
  is open-drain, active low.
- **ALRT:** not connected to a GPIO; SOC is polled instead.
- **[OPEN]:** the 7-bit address wasn't extracted from the text. It's
  expected to be 0x36 and must be confirmed.
- **Deep sleep:** hibernate by default. Sleep mode would save 2.5 µA more,
  at the cost of not tracking while MAO is off. That's a firmware choice.

## 15. Worst-case simultaneous load (§35)

**3V3 rail:**

| Load | Current |
|---|---|
| S3 transmit | 355 mA [DS] |
| Panel logic | ~5 mA [OPEN, panel] |
| Backlight, if on 3V3 | ~20–60 mA [OPEN, panel] |
| IMU | 0.55 mA |
| DRV2605L with LRA | ~100 mA peak [OPEN, LRA] |
| **Total** | **≈ 480–520 mA** |

Within the TPS62840's 750 mA and its 1.0–1.4 A limit. Transient behaviour on
a 355 mA RF step is [TEST] at bring-up, with 22–47 µF bulk capacitance at
the module.

**SYS / battery:**
- **Loads:** the 3V3 path is about 520 mA × 3.3 / (3.7 × 0.9) ≈ 515 mA, plus
  the amplifier at 1 W into 8 Ω (about 300 mA), RGB (about 60 mA) and IR LED
  pulses (about 150 mA).
- **Total:** **≈ 1.0 A.** Within the BQ25185's 3.125 A system discharge.
- **Cell choice:** the cell and its protection must be rated for at least
  about 1.5 A peak. A small cell, around 500 mAh, then needs a 2C+ discharge
  rating, or firmware must never run full-volume audio and haptics at once
  [OPEN, cell].

## 16. Deep-sleep budget (§20–21), component by component

Conversion to battery current: a 3.3 V load draws (3.3 / VBAT) / η from the
battery.
- **Typical:** VBAT 3.7 V, η 80 % (TPS62840) or 70 % (TPS63802).
- **Worst case:** VBAT 3.0 V, η 70 % or 55 %.

**3V3 loads:**

| Item | Typ | Worst | Basis |
|---|---|---|---|
| S3, ext0 + ext1 (RTC peripherals on) | 8.0 | 12 *(no max published)* | [DS] typ |
| ICM-42670-P wake-on-motion | 4.4 | 10 *(allowance)* | **[OPEN]** |
| DRV2605L, EN low | 4.0 | 7.0 | [DS] |
| ENC_A pull, 470 k, resting low | 3.5 *(half the time)* | 7.0 | [TEST] |
| ENC_B (isolated) and ENC_SW (open) | 0 | 0 | design |
| IMU INT leakage (open-drain) | 0.1 | 0.1 | [DS] |
| TPS22916 × 2, off | 0.02 | 0.2 | [DS] |
| USB ESD leakage, board ID, pulls | 0.1 | 1.0 | estimate |
| **3V3 subtotal** | **20.1** | **37.3** | |

**Battery-side totals (µA):**

| | TPS62840, typ | TPS62840, worst | TPS63802, typ | TPS63802, worst |
|---|---|---|---|---|
| 3V3 loads, converted | 22.4 | 58.6 | 25.6 | 74.6 |
| Regulator quiescent | 0.06 | 0.1 | 11 | 20 *(no max)* |
| BQ25185, battery-only | 4 | 5 | 4 | 5 |
| MAX17048, hibernate | 3 | 5 | 3 | 5 |
| MAX98357A, shutdown (on SYS) | 0.6 | 2 | 0.6 | 2 |
| PCB and passive leakage margin | 1 | 3 | 1 | 3 |
| **Total** | **≈ 31 µA** | **≈ 74 µA** | **≈ 45 µA** | **≈ 110 µA** |

**Reading:**
- **With the TPS62840:** the typical paper design is **31 µA, under the
  35 µA design target**. With MAX17048 sleep mode instead of hibernate it's
  about 28.5 µA.
- **With the TPS63802:** 45 µA typical, too tight, as the brief warned.
- **The worst-case columns** stack every maximum at 3.0 V. They show where
  the risk sits (the IMU and the S3 have no published maximum for these
  modes), not an expected result. The ≤ 50 µA requirement applies to the
  **measured** board.

## 17. Production display — still the hard blocker (§26–27)

Nothing was locked, and no connector schematic will be drawn until a vendor
datasheet confirms TE on the FPC. The documents to request:
- the exact part number;
- the FPC pinout, pitch and orientation;
- the mechanical drawing;
- confirmation that the controller is a GC9A01 (or equivalent);
- **TE bonded to the FPC**;
- reset;
- the SPI electrical levels;
- the backlight LED configuration and current;
- VCI and IOVCC ranges.

Draft request for the owner to send to Winstar, DisplayModule, Raystar or
others:

> We are designing a small handheld product around a 1.28" round 240×240
> TFT (GC9A01 or equivalent, 4-wire SPI). Please send the datasheet, FPC
> pinout and mechanical drawing for your panel. We specifically need the
> TE (tearing-effect) output bonded out on the FPC, a separate RESET, the
> backlight LED configuration (count, series/parallel, Vf, rated current),
> VCI/IOVCC ranges and the FPC connector pitch. Prototype quantity
> initially; please include MOQ and lead time.

## 18. Other items

- **RGB and IR:** both on AUX_PWR_EN (one TPS22916). IO33 (RGB data) and IO34
  (IR receiver) are driven low or isolated while the aux rail is off, so
  neither can back-power it (the same rule as the display).
- **IR transmit:** a transistor or MOSFET drives the LED from SYS, with a
  pull-down on its gate. The S3 never drives the LED directly.
- **Capacitor plan (from each vendor's reference design):**
  - S3: 10 µF + 0.1 µF at the module's 3V3 pins, plus 22–47 µF bulk
    (Espressif hardware design guide, to be confirmed at Gate C).
  - TPS62840: per its datasheet (1 µH-class inductor, output capacitance per
    its tables).
  - BQ25185: IN ≥ 1 µF, SYS ≥ 10 µF, BAT per the datasheet.
  - IMU: 0.1 + 2.2 µF and 10 nF.
  - DRV2605L: 1 µF on VDD, plus its REG capacitor.
  - MAX98357A: per its datasheet.
  - Panel: per its datasheet.
- **Enclosure (§37–38):** Gate C can progress without it; **Gate D is
  blocked** until it exists.
- **Current meter (§39–40):** it doesn't block the schematic. It **does
  block** battery sizing, validating ≤ 50 µA, and any runtime claim. No
  runtime figures are given anywhere in this document.

## 19. Gate C readiness (§41)

| | Item | Status |
|---|---|---|
| A | Audio GPIO issue | **closed**: the full I2S was already mapped; MAX98357A verified |
| B | Updated GPIO table with headroom | **closed**: 4 spares (IO26 N8-only, IO37, IO43, IO44) |
| C | Wake architecture | **API proven on IDF 6.0.3, test firmware built; the hardware test is blocked (no ESP32-S3 board)** |
| D | Sleep pull strategy | **selected** (dual-strength, B isolated); **value pending the S3 rig test** |
| E | IMU | **locked** for interface and electrical; wake-on-motion current **[OPEN]** |
| F | Haptic driver | **locked**, DRV2605L, option A |
| G | Charger / regulator / fuel gauge | BQ25185 and MAX17048 locked. **Regulator: owner decision, TPS62840 proposed vs the approved TPS63802** |
| H | Production display with TE | **open: the hard blocker** |

**Gate C isn't started.** It needs:
1. an ESP32-S3 dev board (for C and D);
2. your regulator decision (G);
3. a panel datasheet (H).

Blocks unrelated to the display could be drafted once C, D and G are closed.
