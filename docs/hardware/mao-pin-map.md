# MAO_MAIN A1 pin map

Generated from `hardware/mao/design/pinmap.py` by `gen_pinmap.py`; the same table drives the
schematic nets and `components/mao_board/boards/main_a1/mao_board_pins.h` (ODD JOBS 199).
Flags: strap = boot strapping pin, RTC = usable in deep sleep (wake/hold), touch = capacitive
channel, ADC1 = ADC usable with Wi-Fi on.

## ESP32-S3-MINI-1-N8

| GPIO | Net | Dir | Function | Flags | Notes |
|---:|---|---|---|---|---|
| 0 | — | nc | BOOT strap (pad only) | strap, RTC | S R; 10k pull-up, TP BOOT: the fixture holds it low for download. Nothing else on it |
| 1 | `HAPTIC_EN` | out | haptic driver enable | RTC, ADC1 | R; 100k pull-down (+ the DRV2605L's internal 2M): off |
| 2 | — | nc | spare | RTC, ADC1 | R; test pad |
| 3 | — | nc | JTAG-source strap | strap, RTC, ADC1 | S; NC (inert unless EFUSE_STRAP_JTAG_SEL is burnt, never on MAO) |
| 4 | `IMU_INT1` | od | IMU INT1: wake-on-motion (ICM-42670-P, open-drain, active low, latched) | RTC, ADC1 | R; 100k pull-up; deep-sleep wake (ext1, any-low) |
| 5 | `HALL_FAST` | out | Hall sensors: high = fast sampling, low = low-power | RTC, ADC1 | R; 100k pull-down: low-power from reset and in deep sleep |
| 6 | `LCD_PWR_EN` | out | display logic rail switch (TPS22916C) | RTC, ADC1 | R; switch's smart pull-down + 100k: panel unpowered from reset |
| 7 | `LCD_RST_N` | out | display reset | RTC, ADC1 | R; 100k pull-down: panel held in reset |
| 8 | `LCD_BL` | out | backlight current-sink reference (LEDC ~30 kHz) | RTC, ADC1 | R; 100k pull-down + divider: dark from reset |
| 9 | `AMP_SD` | out | amplifier SD_MODE (high = on, left channel) | RTC, ADC1 | R; 100k pull-down + the MAX98357A's internal 100k: silent from reset |
| 10 | `LCD_CS` | out | display chip select (FSPICS0 IO_MUX) | RTC, ADC1 | R |
| 11 | `LCD_MOSI` | out | display data (FSPID IO_MUX) | RTC | R; 22R series |
| 12 | `LCD_SCLK` | out | display clock (FSPICLK IO_MUX) | RTC | R; 22R series |
| 13 | `LCD_DC` | out | display data/command | RTC | R |
| 14 | `PRESS_N` | in | face-press switch (to GND) | RTC | R; 100k pull-up (draws only while pressed); deep-sleep wake (ext1, any-low) |
| 15 | `TOF_XSHUT` | out | proximity sensor shutdown (low = off) | RTC | R; 100k pull-down: off |
| 16 | `TOF_INT_N` | od | proximity GPIO1: threshold interrupt | RTC | R; 10k pull-up (ST application circuit) |
| 17 | `IR_TX` | out | IR LED driver gate (RMT carrier) | RTC | R; 100k pull-down: LEDs off at reset |
| 18 | `CHG_CE_N` | out | charger /CE: high pauses charging (firmware thermal limit, cell 0-45 C) | RTC | R; 100k pull-down: charging enabled from reset |
| 19 | `USB_DN` | io | USB D- | RTC | native USB-Serial/JTAG; 22R series + DNP 10 pF |
| 20 | `USB_DP` | io | USB D+ | RTC | native USB-Serial/JTAG; 22R series + DNP 10 pF |
| 21 | `HALL_A` | in | ring dial channel A | RTC | R; push-pull from the Hall latch; deep-sleep wake (ext0, armed at the opposite level) |
| 26 | — | nc | spare |  | free on the -N8 (SPICS1 only on -N4R2); test pad |
| 33 | `CHG_STAT1` | od | charger STAT1 (open-drain) |  | 10k pull-up; both STAT pins high-Z on battery: 0 uA |
| 34 | `CHG_STAT2` | od | charger STAT2 (open-drain) |  | 10k pull-up |
| 35 | `LCD_TE` | in | display tearing-effect output: frame sync |  | driven by the panel; isolated while the panel is off |
| 36 | `IR_RX` | in | IR receiver output (RMT) |  | receiver supply switched by AUX_PWR_EN; isolated while it is off. IMU INT2 is not wired on A1: INT1 carries wake-on-motion, the rest is polled |
| 37 | `HALL_B` | in | ring dial channel B |  | push-pull from the Hall latch |
| 38 | `AUX_PWR_EN` | out | IR receiver supply switch (TPS22916C on +3V3) |  | no reset pull; switch's smart pull-down + 100k: off |
| 39 | `VBUS_SENSE` | in | USB VBUS present (100k/150k divider) |  | 0 uA on battery; the stay-awake rule uses USB-Serial-JTAG SOF, this tells charge-only power |
| 40 | `AMP_BCLK` | out | I2S bit clock to the amplifier |  |  |
| 41 | `AMP_LRCLK` | out | I2S word select |  |  |
| 42 | `AMP_DIN` | out | I2S data |  |  |
| 43 | `UART_TX` | out | UART0 TX (service / spare) |  | service pad; the ROM prints here at reset |
| 44 | `UART_RX` | in | UART0 RX (service / spare) |  | service pad |
| 45 | — | nc | VDD_SPI strap | strap | S; NC with its internal pull-down: 3.3 V flash. Nothing may pull it high |
| 46 | — | nc | boot-mode strap | strap | S; NC with its internal pull-down |
| 47 | `I2C_SDA` | io | I2C data (IMU, ToF, gauge, haptic) |  | 4.7k pull-up to +3V3; VDD_SPI/VDD3P3_CPU domain = 3.3 V on the N8 |
| 48 | `I2C_SCL` | out | I2C clock |  | 4.7k pull-up to +3V3 |

33 of 39 module signal pins used; spare: GPIO 0, 2, 3, 26, 45, 46.


## I2C address map (one bus, 4.7 kΩ pull-ups, 400 kHz)

| Address | Device |
|---|---|
| 0x29 | VL53L4CD proximity (default) |
| 0x36 | MAX17048 fuel gauge |
| 0x5A | DRV2605L haptic driver |
| 0x68 | ICM-42670-P IMU (AP_AD0 low) |

No collisions.
