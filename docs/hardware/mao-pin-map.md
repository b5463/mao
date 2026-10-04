# MAO_MAIN A0 pin map

Generated from `hardware/mao/design/pinmap.py` by `gen_pinmap.py`; the same table drives the
schematic nets and `components/mao_board/boards/main_a0/mao_board_pins.h` (ODD JOBS 199).
Flags: strap = boot strapping pin, RTC = usable in deep sleep (wake/hold), touch = capacitive
channel, ADC1 = ADC usable with Wi-Fi on.

## ESP32-S3-WROOM-1-N8R2

| GPIO | Net | Dir | Function | Flags | Notes |
|---:|---|---|---|---|---|
| 0 | `PRESS_N` | in | face-press switch (to GND), BOOT strap | strap, RTC | S R; 10k pull-up; hold at reset = download mode, like the LCDkit knob |
| 1 | `TOUCH_RIGHT` | analog | touch T1, right rim electrode | RTC, touch, ADC1 | T R; 510R series at the module |
| 2 | `IMU_INT1` | in | IMU INT1: wake-on-motion, tap | RTC, touch, ADC1 | R; deep-sleep wake (ext1, active low) |
| 3 | `BOARD_ID` | analog | board revision divider (ADC1_CH2): A0 = 1M/1M + 100 nF = 1.65 V | strap, RTC, touch, ADC1 | S: JTAG-source strap, inert unless EFUSE_STRAP_JTAG_SEL is burnt (never on MAO); read once at boot |
| 4 | `TOUCH_LEFT` | analog | touch T4, left rim electrode | RTC, touch, ADC1 | T R; 510R series |
| 5 | `TOUCH_TOP` | analog | touch T5, window-border electrode (spring) | RTC, touch, ADC1 | T R; 510R series |
| 6 | `TOUCH_REAR` | analog | touch T6, base electrode (spring) | RTC, touch, ADC1 | T R; 510R series |
| 7 | `I2C_SDA` | io | I2C data (all sensors, expander, gauge, haptic) | RTC, touch, ADC1 | R; 2.2k pull-up to +3V3; left-column pin below the I2S group: the bus reaches the haptic driver under the I2S lanes |
| 8 | `USB_PRESENT_N` | in | charger PGOOD (open-drain), low = USB power valid | RTC, touch, ADC1 | R; 100k pull-up; deep-sleep wake |
| 9 | `LCD_DC` | out | display data/command | RTC, touch, ADC1 |  |
| 10 | `LCD_CS` | out | display chip select (FSPICS0 IO_MUX) | RTC, touch, ADC1 |  |
| 11 | `LCD_MOSI` | out | display data (FSPID IO_MUX) | RTC, touch | 22R series option (ODD JOBS 29) |
| 12 | `LCD_SCLK` | out | display clock (FSPICLK IO_MUX) | RTC, touch | 22R series option (ODD JOBS 29) |
| 13 | `MIC_PWR` | out | microphone supply (GPIO-powered through 100R/1uF) | RTC, touch | R; 100k pull-down: mic off at reset. SPH0641 draws 80 uA even with the clock stopped, so it is powered only while listening |
| 14 | `HALL_FAST` | out | Hall sensors: high = fast sampling, low = low-power | RTC, touch | R (held in deep sleep); 100k pull-down |
| 15 | `I2C_SCL` | out | I2C clock | RTC | R; 2.2k pull-up to +3V3 |
| 16 | `AMP_DIN` | out | I2S1 data (left column, with BCLK/LRCLK on the next two pins) | RTC | R |
| 17 | `AMP_BCLK` | out | I2S1 bit clock to the amplifier | RTC | R |
| 18 | `AMP_LRCLK` | out | I2S1 word select | RTC | R |
| 19 | `USB_DN` | io | USB D- | RTC | native USB-Serial/JTAG |
| 20 | `USB_DP` | io | USB D+ | RTC | native USB-Serial/JTAG |
| 21 | `EXP_INT_N` | in | expander interrupt (charger status, gauge/light alerts) | RTC | R; 100k pull-up; deep-sleep wake |
| 35 | — | nc | spare |  | to test pad TP12 (fixture handshake); nothing on the board drives it |
| 36 | `MIC_CLK` | out | I2S0 PDM clock to the microphone |  | right column above the expander reset: runs over the microphone into its far pad 4 |
| 37 | `EXP_RST_N` | out | expander RESET: pulse low to recover a wedged TCA6408A without a power cycle |  | R; 10k pull-up: released from power-on; drive open-drain |
| 38 | `MIC_DATA` | in | I2S0 PDM data from the microphone |  | straight into MK401 pad 1 from below |
| 39 | `IR_TX` | out | IR LED driver gate (RMT carrier) |  | 100k pull-down: LED off at reset (ODD JOBS 117) |
| 40 | `IR_RX` | in | IR receiver output (RMT) |  | 10k pull-up R506 to the receiver supply |
| 41 | `HALL_A` | in | ring dial channel A |  | push-pull from the Hall latch |
| 42 | `HALL_B` | in | ring dial channel B |  |  |
| 43 | `UART_TX` | out | UART0 TX (service) |  | service pad |
| 44 | `UART_RX` | in | UART0 RX (service) |  | service pad |
| 45 | — | nc | spare | strap | S (VDD_SPI strap, must read 0): left NC with its internal pull-down |
| 46 | `LCD_BL_PWM` | out | backlight PWM (LEDC) | strap | S (must read 0 for download boot): 100k pull-down keeps the backlight off |
| 47 | `IMU_INT2` | in | IMU INT2: orientation, free-fall |  |  |
| 48 | `TOF_INT_N` | in | proximity GPIO1: threshold interrupt |  | 10k pull-up (ST application circuit) |

34 of 36 module signal pins used; spare: GPIO 35, 45.

## TCA6408A expander (I2C 0x20)

All expander pins are high-Z inputs without pull-ups from power-on until firmware configures them,
so the external resistors below are the hardware defaults (ODD JOBS 116/117).

| Port | Net | Dir | Function | Power-on default |
|---:|---|---|---|---|
| P0 | `LCD_RST_N` | out | display reset | 100k pull-down: panel held in reset |
| P1 | `LCD_PWR_EN` | out | display + backlight rail load switch | 100k pull-down: off |
| P2 | `AMP_SD_N` | out | amplifier enable (SD_MODE) | 100k pull-down: amplifier shut down |
| P3 | `HAPTIC_EN` | out | haptic driver enable | 100k pull-down: off |
| P4 | `TOF_XSHUT` | out | proximity sensor shutdown (low = off) | 100k pull-down: off |
| P5 | `IR_RX_PWR` | out | IR receiver supply (via RC filter) | 100k pull-down: off |
| P6 | `CHG_N` | in | charger CHG (open-drain): low = charging | 100k pull-up |
| P7 | `SENSE_ALRT_N` | in | fuel-gauge ALRT + light-sensor INT (wired-OR, open-drain) | 100k pull-up |

## I2C address map (one bus, 2.2 kΩ pull-ups, 400 kHz)

| Address | Device |
|---|---|
| 0x20 | TCA6408A expander (ADDR low) |
| 0x29 | VL53L4CD proximity (default) |
| 0x36 | MAX17048 fuel gauge |
| 0x44 | OPT3001 ambient light (ADDR to GND) |
| 0x5A | DRV2605L haptic driver |
| 0x6A | IMU (SA0 low) |

No collisions. Reserved for the DNP FRAM footprint: 0x50.
