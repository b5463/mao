# MAO_MAIN A0: bring-up

First power-on of an unproven board (EVT, 5 units). This procedure is staged: nothing gets more energy
than the previous step proved it can take. Production testing of proven boards is
`mao-factory-test.md`; firmware details are in `docs/firmware/mao-a0-firmware.md`.

Test pads (B side; full table with positions in `mao-factory-test.md` §4):

| Where | Pads |
|---|---|
| Service field, row 1 (below the charger) | TP2 GND, TP3 3V3, TP4 SYS, TP5 BAT |
| Service field, row 2 | TP10 SCL, TP1 GND, TP16 GND |
| Service field, row 3 | TP9 SDA, TP11 XRST, TP8 BOOT, TP7 RST |
| At their sources | TP6 VBUS (at the TVS), TP13 LCDV, TP14 MIC, TP15 IRV |

Tag-Connect J201 (TC2030-NL, marked TAG): 1 GND, 2 EN, 3 TXD0, 4 3V3, 5 RXD0, 6 GPIO0.

**Equipment:**
- current-limited bench supply,
- DMM (µA range),
- USB-C cable and a USB power meter,
- a protected 1S LiPo with a 10 k NTC and the J102 pinout (1 BAT−, 2 NTC, 3 BAT+),
- thermal camera if available,
- ESP-IDF v6.0.3.

## 0. Before any power

1. **Visual / microscope check.** Pin 1 and polarity of:
   - Q101, Q501, U101, U105 (SC70-6)
   - every QFN/DFN: U102 (on the face side, under the panel), U103, U104, U202, U303, U501
   - U401, U402, U403, MK401
   - D101, D301–D304, D501/D502
   - J102 and J101 legs soldered
   - the springs
2. **USB-C:** confirm the receptacle is seated and all four THT shell legs are soldered.
3. **Resistance to GND** (DMM, both polarities; a low reading is a short):

   | Node | Pad | Expect |
   |---|---|---|
   | VBUS | TP6 | > 100 kΩ (charger input) |
   | VSYS | TP4 | > 10 kΩ (UVLO divider 1.51 MΩ, loads off) |
   | VBAT | TP5 | > 100 kΩ |
   | +3V3 | TP3 | > 1 kΩ (I2C pull-ups 2.2 k in parallel with ICs) |
   | 3V3_LCD | TP13 | > 10 kΩ |
4. **BAT link:** confirm R108 (the 0 Ω 1206 link, marked BAT LINK on the back) is fitted. It is the one place to measure battery current: lift it and insert the ammeter across its pads.

## 1. First power: VBUS only, no cell, no panel

Supply on TP6 (+) and D101 pad 2 or TP1 (−): 5.00 V, **current limit 100 mA**.

| Check | Where | Expect | If not |
|---|---|---|---|
| Supply current, ESP32 in ROM boot loop or app | supply | 20–80 mA | > 150 mA: short; stop |
| VSYS | TP4 | 4.35–4.45 V (BQ24073 OUT regulation, no cell) | 0 V: U102 not powered or ILIM/ISET open; check R103/R104/R105 (beside the charger; in the dense power section the passives are identified on `outputs/fab/ASSEMBLY-MAO_MAIN_A0-bottom.pdf`) |
| +3V3 | TP3 | 3.10–3.28 V (3.18 V nominal: FB 536 k / 100 k) | 0 V: check U104 EN (BB_EN must be > 1.1 V when VSYS is 4.4 V); oscillation: scope SW nodes BB_L1/L2 |
| Charger status | GPIO3 (USB_PRESENT_N) | low | |
| Switched rails | TP13 / TP14 / TP15 | < 0.3 V (all off at reset) | a rail on at reset = default resistor missing |
| Temperature | U102, U104 | < 10 °C rise | |

Then raise the limit to 500 mA.

## 2. USB console and first firmware

1. Connect USB-C to the PC with the panel still not fitted. The S3's native USB-Serial/JTAG enumerates without any boot mode.
2. If it does not enumerate, hold the face switch (GPIO0, TP8 BOOT to GND) while plugging in to force the ROM loader. Recovery without USB goes through the Tag-Connect UART. Use a 3.3 V adapter and plug it in only after the board is powered: an adapter's TX idles high and would feed RXD0 of an unpowered module.
3. Flash the dev build: `tools/idf.ps1 s3 dev flash monitor`, or `idf.py -B build-s3-dev flash monitor`.
4. Expected boot log:
   - board ID 1.48–1.70 V (A0: 1.59 V on the 3.18 V rail)
   - `i2c: ... 6/6 devices answered`
   - expander configured
   - rails all off
   - `[--]` lines only for parts that are not fitted yet: the panel
5. Run `mao selftest auto nopads`. Every automatic step must pass, except the display-dependent ones.

## 3. Display

1. Thread the stock Winstar WF0128BTYAA4DNN0's 70 mm tail through the board's slot at 9 o'clock from the face side, fold it inward under the board, open J301's back-flip actuator (bottom side, entry facing the slot), insert the tail and close the actuator (contacts on both faces, so either way up). On the bench, let the excess hang in a loose loop; in the enclosure it S-folds in the face carrier's pocket (`mao-mechanical.md` §4). No soldering: a panel can be swapped the same way.
2. Check TP13 (LCDV) switches 0 → 3.18 V when the firmware enables the rail, with a soft start of about 1.7 ms (U105 TPS22919, fixed slew; scope it: no dip on +3V3 > 100 mV).
3. **Orientation:** the default rotation is 90°, set from the 9 o'clock FPC exit. If the face is rotated, run `mao rotate 270` (or 0 / 180), then `mao rotate save`. Record the value and make it the Kconfig default.
4. Check colour order and inversion against the LCDkit (same GC9A01 controller; confirm `A0_LCD_PANEL_MIRROR_X` and the colour order on the fitted panel and record them).

## 4. Battery and charger

1. Connect the protected cell to J102. Check polarity before plugging: the plug's red lead must go to the pin marked + on the silkscreen (pin 3).
2. **Without USB:** VBAT at TP5 = cell voltage − Q101 drop (< 20 mV at light load). The board runs from the cell, and the current drawn should match `mao-power-budget.md` per state.
3. **With USB:** charge current through R108's pads (link lifted, ammeter inserted) is 185–227 mA (207 mA typical, ISET R103 4.3 kΩ; the LP503035 cell allows 250 mA), falling in the constant-voltage phase. `mao power` reports `charging` (from PGOOD and the gauge's charge rate).
4. **Charge pause (/CE):** with charging running, `mao power charge off` (dev build; drives expander P6 high): the current must fall to 0 within a second, and come back after `mao power charge on`. R117 (100 kΩ) holds /CE low, so charging is enabled whenever the expander is in reset. Then warm the board (hot air on the IMU side, carefully) past 43 °C board temperature: firmware pauses charging and logs it; below 40 °C it resumes.
5. **NTC:** warm the cell's NTC to above 50 °C (hot air, carefully) or swap in a 3.3 k resistor: charging must stop. Cooling (or 27 k) must also stop it below 0 °C.
6. **UVLO:** feed VSYS from a supply through TP4 with USB removed and the cell disconnected. Ramp down: +3V3 must drop at 2.96 V ± 4 % and restart at 3.25 V ± 4 % (room temperature; the worst case over temperature is in simulation S1).
7. **Gauge:** `mao power` shows VCELL within 20 mV of the DMM. SOC converges within one charge cycle.

## 5. Each subsystem

| Subsystem | Test | Pass |
|---|---|---|
| Speaker | self-test `speaker` step (chirp heard by the mic) and the UI sounds | clean, no clipping at full volume; amplifier silent and 0.6 µA when SD is low |
| Haptics | `mao haptic calibrate`, then `mao haptic <name>` | resonance 190–285 Hz, the effects feel distinct |
| Microphone | `mao sense` (mic level) | quiet room −58 dBFS ± 6; speech peaks −30 dBFS |
| IMU | `mao sense` (accelerometer) | +1 g on z face-up (else set `MAO_PERCEPT_IMU_Z_DOWN`) |
| ToF | `mao sense` (distance), window fitted | 50–1300 mm on a hand. Run crosstalk calibration with the window in place |
| Light | `mao sense` (lux) | covered < 3 lux, office 200–800 lux. The Ø2 window aperture narrows the view to about ±17°: record the lux scale factor against a meter |
| Touch | `mao sense` (touch) | each zone ≥ 2 % change through the enclosure wall; LEFT with the speaker fitted, and steady while a sound plays |
| Backlight | on the cell (USB unplugged), ammeter in R108's place: battery current at brightness 100 % minus at 0 % (U303 AW9364 is a linear sink from VSYS, so the difference is the LED current) | 33–47 mA (40 mA nominal: 2 × 20 mA, ±17.5 % part tolerance), steady while VSYS > 3.6 V on any panel bin (S3). `mao backlight step 1 … 16` steps down in sixteenths; record the value |
| Ring | gaussmeter at the Hall height with the ring off, then turn the ring | strip ≥ 8 mT peak; 30 steps per turn, each with a haptic tick; three turns read 90 ± 1 (S11); direction correct (else swap in firmware) |
| IR | `mao ir rx on`, `mao ir send <addr> <cmd>` under the fixture lid | RX sees TX through the lid reflection; a TV remote is received |
| Radio | ODD BUS self-test / flood tool (M2) against LAMP at 1 m / 5 m, board in the enclosure | RSSI within 6 dB of the LCDkit at the same distances |
| Enclosure fit | assemble with the printed shell, the flexure-hung face (window, panel, carrier), ToF gasket, mic tube and the speaker in its cradle (`mao-mechanical.md` §1, §4, §5, §8) | press the face at the centre and at the rim all round: one clean click everywhere, never touching the IR receiver dome; the TOP spring presses its boss; the ToF gasket seats on the sensor cap; the speaker's contacts press on LS501 (speaker test passes) |

## 6. Power measurements (fill in `mao-power-budget.md`)

Through R108's pads with the link lifted, on the cell (4.0 V), USB unplugged:

| State | How | Estimate |
|---|---|---|
| Active | face on, radio listening | ~133 mA |
| Idle | dimmed face | ~116 mA |
| Drowsy | panel sleep, light sleep | ~1.6 mA |
| Deep sleep | `mao sleep` | ~73 µA incl. PCM (board only, without the cell's PCM: ~58 µA) |

A deep-sleep figure above 150 µA means a rail or pull-up is leaking. Find it by lifting one rail at a time: check the test pads TP13–15 read 0 V.

## 7. Known risks to watch on A0

| Area | Risk | How it shows | Fallback |
|---|---|---|---|
| Buck-boost | layout per TI, but a 0.47 µH part with 2 × 22 µF 0603 at 3.3 V bias | ripple > 50 mV, audible whine | add the second COUT footprint value 22 µF → 47 µF 0805 in A1 |
| Charger heat | 0.72 W worst case at 3.0 V cell (207 mA); U102 sits on the face side under the panel | U102 > 70 °C | ISET to 6.2 k (145 mA) by a resistor swap |
| Antenna | battery and enclosure detuning | RSSI more than 6 dB below the LCDkit | none on the board (no matching network on the module); move the cell or reduce plastic thickness near 6 o'clock |
| Touch | 2 mm wall + ring between arc and finger | change < 1 % | conductive filament as material B, or copper tape on the wall |
| Ring | ferrite pole strip field at the sensors (≥ 8 mT specified; they switch at ±3.3 mT) and FDM gap tolerance | missed steps | reduce the gap, or a thicker / stronger strip |
| ToF | window crosstalk | false "near" | thinner window, black mask gap around the aperture, crosstalk calibration |
| Footprints not yet proven on a board | JST SH clone (J102), DFE201612E land (L101), HDGC 0.5K-HX-18PWB FPC connector (J301), BW0019BG springs, AW9364 DFN (U303) | poor solder joints | check the first boards under the microscope; footprints come from the datasheets in `hardware/mao/lib` |
| Display tail | the 70 mm tail through the slot and the S-fold in the carrier pocket | panel blank or flickers when the face is pressed | re-seat the tail; check the fold radius at the slot (≥ 1 mm) and that the S-fold does not load the connector |
| Backlight current | AW9364DNR ±17.5 %: up to 47 mA on a high unit (panel 40 mA typical) | panel hotter than the LCDkit's at full brightness | firmware maps 100 % to step 2 (37.5 mA nominal) if the panel supplier rates the LEDs below 47 mA |
