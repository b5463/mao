# MAO_MAIN A0: architecture

MAO_MAIN A0 is the first custom MAO board (engineering prototype, EVT). It replaces the
ESP32-C3-LCDkit and gives MAO enough perception to notice the world before it is touched. The design is
code: `hardware/mao/design/circuit.py` (capture), `placement.py` + `mechanical.py` (layout), and
the routing toolchain ported from the KINO D4 carrier. The ODD JOBS standard (`hardware/mao/ODD-JOBS-STANDARD.txt`)
governs; rule numbers below refer to it.

## 1. Product form

A round puck, ~Ø64 × 18.3 mm, FDM-printed in up to two materials (Bambu X2D, 2 × AMS HT).

```
                 back (12 o'clock): USB-C, IR out
            ┌──────────────────────────────┐
         ╭──┤  rotating ring dial (30 pole)  ├──╮
        │   │   ╭──────────────────────╮    │   │
  touch │   │   │  cover window (clear) │    │   │ touch
  LEFT  │   │   │   ◉        ◉  face    │    │   │ RIGHT
        │   │   ╰──────────────────────╯    │   │
         ╰──┤        press the face         ├──╯
            └──────────────────────────────┘
                 front (6 o'clock): antenna
```

| Element | Decision | Why |
|---|---|---|
| Face | Plug-in 1.28" round GC9A01 panel (Winstar WF0128BTYAA4DNN0, 240 × 240 IPS) on an 18-pin 0.5 mm FPC tail in the J301 back-flip connector, centred on the puck axis | Swappable without solder; same controller and resolution as the LCDkit's panel, so the character's colours and timings carry over (orientation constants verified at bring-up) |
| Dial | Ring around the face, 30-pole flexible ferrite strip + 2 × DRV5012 Hall latches; each step is an LRA tick | Same electrical behaviour as the LCDkit EC11 (30 detents, 15 quadrature cycles, rest at 00/11), so `mao_input`'s decoder is unchanged. Contactless, thin, no gear backlash, and no metal sweeping over the antenna (the ring passes over it at 6 o'clock) |
| Press | The face (window + panel on a printed carrier) hangs on three printed flexures and rocks on its lip onto an ALPS SKQG switch at the PCB centre (0.25 mm at the centre, 0.5 mm at the rim), with an LRA click on every press | Pressing MAO's face is the button; no sliding fit, which FDM cannot hold. It stays on GPIO0, so holding it while plugging USB enters the ROM bootloader, as with the LCDkit knob |
| Body touch | LEFT/RIGHT: copper arcs at the board rim sensing through the ring; TOP: spring to a window-border electrode; REAR: spring to a base electrode | Hidden sensing (brief §8); printed electrodes possible with conductive filament as the second material |
| Sensor window | 6 mm band of the cover window around the panel, masked except over the sensors | Proximity at 11, light at 1, IR receive at 3 o'clock look out through it |
| Antenna | Module at 6 o'clock, antenna over a notch in the board edge | Espressif placement; opposite the power section and USB (ODD JOBS 2–6) |

Mechanical datums: `hardware/mao/design/mechanical.py`; drawing and enclosure interface: `mao-mechanical.md`.

## 2. MCU decision

**ESP32-S3-WROOM-1-N8R2** (8 MB flash, 2 MB quad PSRAM, −40…85 °C) plus an **8-bit I2C GPIO expander**
(TCA6408A).

| Requirement | ESP32-C3 (LCDkit) | ESP32-S3 |
|---|---|---|
| Signals needed (42) | 15 usable GPIO: impossible | Every module GPIO used natively except the strap GPIO46 (NC), incl. USB, UART and the display TE line; GPIO45 (VDD_SPI strap) drives the backlight gate behind its 100 k pull-down; + 8 on the expander |
| Body touch | none | 14 native channels, deep-sleep touch wake |
| Microphone | no PDM RX | I2S0 PDM RX with hardware PDM→PCM |
| Speaker + mic together | one I2S | I2S0 (mic) + I2S1 (amp) concurrently |
| Animation / audio headroom | 400 KB SRAM | 512 KB SRAM + 2 MB PSRAM, dual core 240 MHz |
| ESP-NOW with LAMP (C3) | — | same protocol, same channel (verified) |

Why WROOM-1 over MINI-1: KiCad has a verified WROOM-1 footprint (MINI-1 has none). Also, the MINI-1 lacks an 8 MB + PSRAM
variant, and quad-PSRAM WROOM keeps GPIO35-37 usable and the 85 °C rating (octal variants are 65 °C).
Why an expander and not a bigger MCU: the user chose it over an ESP32-P4 + radio companion (two
chips, split radio stack). Every slow line moves there; every wake or fast line stays native.

## 3. Power

```
USB-C ─ ESD/TVS ─ VBUS ─► BQ24073 (linear, DPPM, USB500, 297 mA) ─► VSYS (4.4 V on USB, ≈VBAT on battery)
                                   │ BAT                               ├─► TPS63802 buck-boost ─► +3V3 (2 A)
                     cell (PCM, NTC) ─ R108 0R link ─ Q101 RPP ─ VBAT   │      EN = UVLO divider: on 3.26 V / off 2.96 V
                                   │                                    ├─► MAX98357A speaker amp
                     MAX17048 gauge on VBAT                             ├─► DRV2605L haptic driver
                                                                        └─► 2 × IR LEDs
+3V3 ─► TPS22917 ─► 3V3_LCD (panel + backlight, soft start, discharge)
GPIO35 ─ RC ─► MIC_VDD      expander P5 ─ RC ─► IR_RX_VCC
```

| Topic | Decision |
|---|---|
| Charger | BQ24073: linear (no second switch node near the mic and IMU), runs MAO while charging (DPPM), CHG/PGOOD to the MCU, NTC window 0–50 °C, timers. BQ24074 drop-in via R105 |
| 3.3 V | TPS63802 buck-boost: uses the whole cell (an LDO would strand the last 10–15 %), 11 µA Iq, precise EN used as hardware UVLO |
| Battery protection | Cell PCM (mandatory in the cell spec, checked on receipt) + hardware UVLO + NTC charge window + firmware cut-off from the gauge + reverse-polarity P-FET. No second protector IC: the pin-out of the common FS8205A could not be verified, and the UVLO covers over-discharge (ODD JOBS 49/51) |
| Fuel gauge | MAX17048 ModelGauge: real SOC without a sense resistor, 3 µA hibernate |
| Rail control | Display (load switch), mic (GPIO supply), IR receiver (expander supply), amp (SD), haptic (EN), ToF (XSHUT), Hall sampling (SEL). Each can be off; all are off at reset. The three switched rails have probe pads (TP13–15) so the fixture can prove they switch |
| Expander recovery | GPIO38 drives the TCA6408A RESET (10 k pull-up): firmware resets and reprograms a wedged expander without a power cycle |
| USB-C | Sink only (2 × 5.1 kΩ Rd), USB 2.0 FS to the S3's native USB-Serial-JTAG (flashing, console, JTAG, recovery), TPD2E2U06 + SMF15A at the connector |

Power budget and runtimes: `mao-power-budget.md`.

## 4. Perception hardware

| Sense | Part | Interface | Wake source |
|---|---|---|---|
| Motion, orientation, tap, free-fall | LSM6DSOX | I2C 0x6A, INT1 (GPIO14), INT2 (GPIO47) | yes (INT1, wake-on-motion) |
| Approach | VL53L4CD ToF, 0–1.3 m | I2C 0x29, GPIO1 → GPIO48, XSHUT via expander | via threshold interrupt (light sleep, GPIO wake) |
| Light / covered | OPT3004 | I2C 0x44, INT wired-OR | — |
| Hearing | SPH0641 PDM mic | I2S0 PDM RX (GPIO37 clock, GPIO36 data) | — |
| Touch (4 zones) | ESP32-S3 native | TOUCH1/4/5/6 | yes (TOP channel) |
| Dial | 2 × DRV5012 | GPIO41/42 | — |
| Press | SKQG | GPIO0 | yes |
| Power events | BQ24073 PGOOD/CHG, MAX17048 ALRT | GPIO3, expander | yes (USB plug, alerts) |
| IR | IRM-H638T | RMT RX GPIO40 (TX GPIO39) | — |

Firmware data flow (see `docs/firmware/mao-a0-firmware.md`):

```
drivers (mao_sense, mao_power, mao_input) → observations → mao_perception (filters, hysteresis,
fusion windows, confidence) → interpreted events (APPROACH_NEAR, PICKED_UP, GENTLE_PET, …) →
mao_events → mao_app (cause/state) → mao_character / mao_audio / mao_haptics
```

The character never reads hardware; a sensor never maps straight to an animation (brief §29/30).

## 5. Feedback

| Output | Part | Notes |
|---|---|---|
| Speaker | MAX98357A I2S class-D from VSYS, 9 dB, into a Same Sky CMS-150803-088S-X8 (15 × 8 × 3 mm, 8 Ω) whose own spring contacts press on pads LS501 | replaces the always-on NS4150: 0.6 µA shutdown, no PDM RC filter, same synthesised vocabulary; the 15 × 8 mm speaker is what fits beside the cell |
| Haptics | DRV2605L + LD0832AA LRA (235 Hz) | closed-loop, auto-resonance; vocabulary tick/heartbeat/annoyed/… |
| IR | 2 × IR12-21C side-emitting at the back edge | 42–66 mA pulses, NMOS low side |
| Status LED | **removed** | the WS2812 only flashed tiny pulses; the face and haptics carry that now (no gratuitous RGB, brief §45/46) |

## 6. Board

- Ø58 mm disc, 1.6 mm, **4 layers** (JLCPCB standard JLC04161H-1080: 1080 prepreg, so L1 sits 0.076 mm over L2): **L1 (F)** parts and the critical lines (USB pair, IMU, touch leads, PRESS) / **L2 (In1) solid GND**: no tracks, no splits; only the touch-arc cuts and the antenna keep-out shape it / **L3 (In2) power**: a +3V3 plane over most of the board, a VSYS band from the power section along the 12 o'clock edge and down the 9 o'clock rim, a VBUS strip under the receptacle, plus the slow lines (enables, interrupts, resets, the I2C trunk) / **L4 (B)** parts and short fan-outs over the L3 planes. USB, the display SPI, I2S and the PDM clock never touch L3. The first A0 was routed on 6 layers; this board was re-placed from scratch for 4 layers with designed escapes (`design/route_local.py`), so every fast line keeps one outer layer over an unbroken reference and the board needs fewer vias than the 6-layer one.
- Power section at 1–2 o'clock on B, laid out by hand (`design/route_power.py`): one straight chain from the cell
  (J102) through the 0 Ω link R108 and reverse-polarity FET Q101 to the charger, then VSYS straight into the
  buck-boost. The buck-boost follows TI's reference layout: inductor over the power pins, CIN at VIN,
  COUT at VOUT, a PGND strip between the pin rows with a via under the IC. VSYS reaches the amplifier,
  haptics and IR on F.Cu over the In1 ground plane.
- Probe pads on B where their signals are: a service field between the charger and the module (GND, 3V3, SYS, BAT,
  SDA, SCL, XRST, BOOT, RST on a 3.0 × 3.2 mm grid, a ground beside every rail), VBUS at the USB TVS,
  LCD/MIC/IRV at their switches. Tag-Connect TC2030-NL at the UART pins. Supplies and grounds on 1.2 mm pads, signals on 1.0 mm.
  Every IC, capacitor and connector GND / +3V3 pad reaches its plane through a via of its own or one shared with a
  neighbouring pin of the same net within 1.6 mm; pull-down resistors join GND through the outer pour, and every pour
  fragment is stitched to L2 (ODD JOBS 14/16/17).
- Antenna notch 24 × 6.3 mm; no copper, track, via or pour within 3 mm of it on any layer (rule area + the module's own keep-out).
- Two M2 screws (heat-set inserts in the chassis) on the back half, one plastic locating peg on the antenna half (ODD JOBS 69).
- Matte black solder mask, white silk, ENIG (visible product board; rule 173 caveat noted for inspection).
- Design rules inside JLCPCB standard capability with margin: 0.15 mm track/space minimum (0.2 default), through vias only: 0.6/0.3 mm for planes and power, 0.5/0.2 mm for the designed fan-out at fine-pitch parts and the module pin rows, 0.2 mm thermal vias in the exposed pads.
- Silkscreen: maker's mark and identity on both faces (MAO / MAIN A0 / 2026-10, S/N box on the back), connector and test-pad names by function, references for every IC, connector, transistor, diode and for each passive a service procedure names; the remaining resistors and capacitors are on the assembly drawing (`outputs/fab/ASSEMBLY-*.pdf`), see `design/mao_labels.py`.

## 7. What was evaluated and left out

| Candidate | Decision | Reason |
|---|---|---|
| External RTC | no | the S3 RTC (RC slow clock, ±5 %) keeps "how long was I asleep" to minutes over hours; timers survive deep sleep |
| FRAM | no | 8 MB flash with a 24 KB+ NVS partition holds character memory; writes are infrequent and wear-levelled. No DNP footprint: nothing on this board earns a speculative site |
| Second protector IC | no | see Power |
| Magnetometer | no | the ring's pole strip, the speaker and the LRA make it useless |
| Extra buttons, RGB LEDs, headers | no | brief §46, ODD JOBS minimalism |
| Ring encoder IC (TMAG5110) | no | 6 mA continuous; two DRV5012 give the same quadrature at 1.6 µA asleep |
| 32 kHz crystal | no | not needed for behavioural time gaps; frees GPIO15/16 |
