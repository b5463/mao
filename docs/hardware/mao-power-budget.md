# MAO_MAIN A0 power budget

Estimates from datasheet typicals at 25 °C, before measurement. The bring-up procedure
(`mao-bringup.md`) measures every row through the 0 Ω battery link R108 (ODD JOBS 110) and
replaces these numbers.

## Assumptions

- Cell: 1S LiPo, 500 mAh (PKCELL LP503035 class), 3.7 V nominal, own protection board (PCM
  quiescent current ≤ 8 µA).
- 3.3 V from the TPS63802 buck-boost: ~90 % efficient at 10–500 mA, ~80 % below 1 mA.
  Battery current = I(3V3) × 3.3 / (η × 3.7).
- Amplifier, haptic driver and IR LEDs run from VSYS (= VBAT on battery) and are counted directly.
- Radio: ESP-NOW receive listening with Wi-Fi power save **off**, as the M2 firmware does today. The
  "radio PS" column assumes an ESP-NOW wake window (`esp_now_set_wake_window`): a firmware choice,
  not hardware.
- Backlight: 2 parallel white LEDs, 10 Ω from 3V3_LCD, ~33 mA at full PWM (Vf 3.0 V typ). Firmware caps PWM at 80 %,
  so 100 % brightness is ~26 mA (17–50 mA × 0.8 across panel Vf bins).

## Per subsystem

| Subsystem | Part | Active | Idle | Drowsy / light sleep | Deep sleep / off |
|---|---|---:|---:|---:|---:|
| MCU + radio | ESP32-S3-WROOM-1-N8R2 | 100 mA (RX listening, 240 MHz); 355 mA TX peaks | 95 mA | 0.3–2 mA (light sleep, radio off or windowed) | 8 µA |
| LCD panel | WF0128BTYAA4DNN0 (GC9A01) | 8.5 mA | 8.5 mA | 15 µA (sleep-in) | 0 (rail switched off) |
| Backlight | 2 LEDs, AO3400A PWM | 18 mA (70 %) | 9 mA (35 %) | 0 | 0 |
| Display rail switch | TPS22917 | 0.5 µA | 0.5 µA | 0.5 µA | 0.5 µA |
| Speaker amp | MAX98357A | 2.4 mA idle; 20–150 mA while a sound plays | 0.6 µA (SD low between sounds) | 0.6 µA | 0.6 µA |
| Microphone | SPH0641LU4H-1 | 0.62 mA (2.4 MHz) / 0.24 mA (768 kHz LP) | 0.24 mA or off | 0 (GPIO supply off) | 0 |
| IMU | LSM6DSOX | 0.55 mA (XL+G 104 Hz) | 30 µA (XL 52 Hz LP) | 4.5 µA (wake-up mode) | 4.5 µA (wake-on-motion armed) |
| Proximity | VL53L4CD | 5 mA (100 ms period, 20 ms budget) | 0.5 mA (1 Hz autonomous) | 5 µA (XSHUT) | 5 µA |
| Ambient light | OPT3004 | 1.8 µA | 1.8 µA | 0.3 µA (shutdown) | 0.3 µA |
| Body touch | ESP32-S3 touch, 4 ch | 0.5 mA (continuous scan) | 0.2 mA | 18 µA (1 ch, 1 % duty) | 18 µA (TOP wake channel) |
| Ring Hall sensors | 2 × DRV5012 | 0.31 mA (2.5 kHz) | 0.31 mA | 3.2 µA (20 Hz) | 3.2 µA |
| Haptic | DRV2605L + LRA | 0.5 mA enabled; 60–100 mA while playing | 4 µA (EN low) | 4 µA | 4 µA |
| IR receive | IRM-H638T | 0.4 mA when listening | 0 (unpowered) | 0 | 0 |
| IR transmit | 2 × IR12-21C | 2 × 26–59 mA pulses, 33 % carrier duty, only while sending | 0 | 0 | 0 |
| Fuel gauge | MAX17048 (on VBAT) | 23 µA | 23 µA | 3 µA (hibernate) | 3 µA |
| Charger | BQ24073 (battery drain, no USB) | 4.3 µA | 4.3 µA | 4.3 µA | 4.3 µA |
| 3V3 regulator | TPS63802 Iq | 11 µA | 11 µA | 11 µA | 11 µA |
| UVLO divider | 470 k / 240 k on VSYS | 5.9 µA | 5.9 µA | 5.9 µA | 5.9 µA |
| GPIO expander | TCA6408A | 1 µA | 1 µA | 1 µA | 1 µA |
| Board-ID divider | 1 M / 1 M on 3V3 | 1.7 µA | 1.7 µA | 1.7 µA | 1.7 µA |
| Default pull-downs | 100 k on enabled lines | ~0.2 mA (6 lines high) | ~0.1 mA | 0 | 0 |

## Totals and runtime (500 mAh)

| State | What runs | 3V3 load | Battery current | Runtime |
|---|---|---:|---:|---:|
| **Active** | face on 70 %, 30 fps animation, all perception sensors, radio listening | ~134 mA | **~133 mA** | **~3.8 h** |
| Active, radio PS | as above with an ESP-NOW wake window | ~75 mA | ~75 mA | ~6.5 h |
| **Idle** | face dimmed 35 %, low frame rate, proximity 1 Hz, mic off | ~116 mA | **~116 mA** | **~4.3 h** (radio PS: ~9 h) |
| **Drowsy** | panel sleep-in, light sleep, motion/touch/press/USB wake, radio windowed | ~1.5 mA | **~1.6 mA** | **~13 days** |
| **Deep sleep** | everything off except wake sources (press, IMU motion, USB plug, expander INT, TOP touch, RTC timer) | ~55 µA | **~73 µA** (incl. PCM) | **~9 months** |
| Peak | TX burst + backlight + amp + haptic + IR together | — | ~0.9 A for < 10 ms | — |

Runtimes use the 500 mAh nameplate. An aged or cold cell gives about 15 % less (simulation S15 in
`mao-a0-verification.md` uses 85 %: active ~3.2 h, drowsy ~11 days, deep sleep ~8 months).

**Conclusions**
- The radio dominates. On battery, firmware should use an ESP-NOW wake window and drop to Drowsy quickly:
  MAO is mostly a desk object and most of its life is on or near USB.
- Deep sleep at ~73 µA lets MAO sit on a shelf for months and still wake by motion, touch or a press.
  The PCM is the floor; the UVLO (2.96 V) stops the 3V3 rail before the PCM has to act.
- Peaks (~0.9 A) are within the JST SH contact rating (1 A), the AO3401A reverse-polarity FET (4 A),
  the 1206 0 Ω link (2 A class) and the TPS63802 (2 A). The firmware should not start a haptic effect
  and a loud sound in the same 10 ms window while transmitting IR (the power HAL serialises them).

## Charging

USB500 input (≤ 500 mA from any USB-C source with Rd only), 297 mA charge, DPPM: MAO runs from USB while
charging and the charge current yields to the system load. Charger dissipation worst case 0.85 W at
VBAT 3.0 V (≈ +38 °C on the VQFN with its thermal vias); typical 0.57 W at 3.7 V. Full charge of a 500 mAh
cell ≈ 2 h plus the constant-voltage tail.
