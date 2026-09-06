# Waveshare ESP32-S3-Touch-LCD-1.47 Build

This variant replaces the Xiao's external OLED and two physical buttons with the Waveshare ESP32-S3-Touch-LCD-1.47's built-in 172x320 capacitive touchscreen, and drives the fan with PWM speed control rather than plain on/off. The fan circuit topology is the same as the Xiao build (2N2222A or BC337 low-side switch on the fan's ground return, flyback diode, dedicated ground bonding wire), but the base resistor and diode are sized for PWM here -- see [Fan drive](#fan-drive-pwm-speed-control) below. The ground-bonding warning in [circuit-schematic.md](circuit-schematic.md) applies unchanged.

## Reserved pins (do not reuse)

These are wired internally on the board to the display and touch controller, confirmed from Waveshare's own Arduino example and community demo code for this exact board:

| Signal | GPIO |
| --- | --- |
| LCD MOSI | 39 |
| LCD SCK | 38 |
| LCD CS | 21 |
| LCD DC | 45 |
| LCD RST | 47 |
| LCD backlight | 46 |
| Touch SDA | 42 |
| Touch SCL | 41 |
| Touch RST | 47 (shared with LCD RST) |
| Touch INT | 48 |
| BOOT button | 0 |
| Battery ADC (unused here, no battery in this build) | 12 |

The module is an ESP32-S3R8 (8 MB octal PSRAM) with 16 MB flash, native USB (no separate USB-UART bridge chip), confirmed via `platformio.ini`'s `qio_opi` memory type setting.

## Our wiring (fan + thermistor)

```text
                         AD5X MAINBOARD AUX FAN PORT (BACK PANEL)
                         (VERIFY POLARITY AND VOLTAGE)

             +24 V  o----[ 1 A fuse ]----+--------------------+
                                         |                    |
                                         |                 +--+--+
                                         |                 | 24 V |
                                         |                 | FAN  |
                                         |                 +--+--+
                                         |                    |
                                         |                    +------|<|------+
                                         |                    |   flyback     |
                                         |                    |   diode       |
                                         |                    |               |
                                         |                    |  fan GND, run forward
                                         |                    |  to the board
                                         |                    |               |
                                         |                              C  +---+---+
                                         |         GPIO5 --[220R]--------B  |2N2222A|
                                         |                              E  +---+---+
                                         |                                  (or BC337)

             2N2222A (TO-92) physical pinout -- flat/marked face toward you,
             leads pointing down. Confirmed on the actual parts in hand:

                       +-------+
                       |2N2222A|         E (left)   -> common ground
                       +-------+         B (middle) -> 220R -> GPIO5
                        |  |  |          C (right)  -> fan GND return
                        E  B  C

             Viewed from the rounded back instead, this order mirrors to C-B-E.
                                         |                                    |
             GND   o--------------------+------------------------------------+--- GND

                         THERMISTOR INPUT (3.3 V ADC)

             3V3 o----[ 10 kOhm NTC, Beta 3950 ]----+----[ 10 kOhm ]----o GND
                                                     |
                                                     +---------------------- GPIO4

             Optional: 100 nF capacitor from GPIO4 to GND, placed near the board.

                         POWER (from printer display USB-A, via this board's USB-C)

             USB-A +5 V  o---------------------------------- board USB-C (VBUS)
             USB-A GND   o---------------------------------- board USB-C (GND)
```

## Connections

| Circuit point | Connect to |
| --- | --- |
| AD5X auxiliary fan `+24 V` | Fuse input, fan `+24 V` |
| Fan `GND` (motor return) | 2N2222A collector |
| 2N2222A emitter | Common ground |
| Board `GPIO5` | 220 Ohm base resistor, then 2N2222A base |
| 1N5819 Schottky cathode | Fan `+24 V` |
| 1N5819 Schottky anode | Fan `GND` / 2N2222A collector |
| Thermistor divider midpoint | Board `GPIO4` |
| Printer display USB-A `+5 V` / `GND` | Board's own USB-C connector |

## UI

The screen runs in portrait orientation (172 wide x 320 tall) with the USB-C port at the bottom. It shows the current temperature, the setpoint, and fan state, with two large on-screen buttons at the bottom of the screen (`-` and `+`) that nudge the setpoint by 0.5 degree C per tap -- the same step size, debounce behavior, and NVS persistence as the physical buttons in the Xiao build. The web UI (same page/settings routes as the Xiao build) is also still available over Wi-Fi.

**Orientation is a firmware assumption, not a confirmed fact** -- `DISPLAY_ROTATION` in `src/main_s3touch.cpp` is set to `2` based on the vendor demo's default (`0`) placing the USB-C at the top; rotating 180 degrees should put it at the bottom. Verify this on first boot: if the UI renders upside down, change `DISPLAY_ROTATION` to `0` and re-flash. The touch driver is initialized with the same rotation value so touch coordinates stay aligned with what's drawn.

## Fan drive (PWM speed control)

The fan is a 2-wire 24 V unit rated 0.29 A @ 24 V over a 16-26.4 V supply range, and its speed is controlled by chopping its supply with the 2N2222A (or BC337) at 25 kHz. Speed ramps proportionally between `setpoint - lower threshold` (idle) and `setpoint + upper threshold` (the configured maximum), with a 25% duty floor so the fan doesn't sit below its stall speed. All the knobs are `#define`s at the top of `src/main_s3touch.cpp`:

| Define | Default | Purpose |
| --- | --- | --- |
| `FAN_PWM_ENABLED` | `1` | Set to `0` for plain on/off switching |
| `FAN_PWM_FREQUENCY_HZ` | `25000` | Above the audible band; lower it to cut switching loss |
| `FAN_PWM_RESOLUTION_BITS` | `8` | LEDC duty resolution (0-255) |
| `FAN_MIN_DUTY_PERCENT` | `25` | Stall floor once the fan is asked to run |
| `FAN_SPEED_CAP_MIN_PERCENT` | `25` | Lower bound of the web UI's max-speed setting |

Unlike the earlier 4-wire PWM design, this drive is **not** inverted: `GPIO5` high turns the 2N2222A on and powers the fan, so duty maps straight through.

### Component choices for PWM

- **2N2222A (TO-92), or BC337**: at 0.29 A and a 26.4 V worst-case rail these are the only suitable NPNs from the on-hand parts, and they're interchangeable in this circuit. The 2N2222A is the default here because it's a purpose-built switching transistor and turns off faster, which suits 25 kHz PWM slightly better. Note that a TO-92 part marked "2N2222A" is really a PN2222A/P2N2222A equivalent (about 600 mA, 40 V) rather than the 800 mA TO-18 original -- still ample for 0.29 A. Rejected alternatives: the S8050's 25 V Vceo sits below the fan's own maximum supply voltage; the 2N3904 (200 mA) and C1815/A1015 (150 mA) can't carry the current; the rest of the on-hand parts are PNP.
- **Pinout**: the TO-92 2N2222A in hand is **E-B-C left to right** with the flat face toward you and the leads down (the standard P2N2222A order), as drawn above. If you substitute a BC337, re-check first -- TO-92 pin assignments vary between part families and manufacturers, and a reversed emitter/collector still conducts weakly (reverse beta of only a few), so it looks half-working while running hot. To verify any unknown part: in diode mode the base is the pin reading ~0.7 V forward to both others, and an hFE socket reads in the hundreds when oriented correctly versus single digits when reversed.
- **220 Ohm base resistor**: gives roughly 11 mA of base drive (forced beta ~26 at 0.29 A), enough to saturate either transistor while staying well inside the ESP32-S3's per-pin current limit. The earlier 330 Ohm value was sized before the fan's real current was known and is too weak here.
- **1N5819 Schottky flyback**: required, and it must be fast. Under PWM this diode conducts 25,000 times a second; a standard rectifier like a 1N4007, whose reverse recovery is measured in microseconds, would spend a large fraction of each cycle effectively shorting the rail. The Schottky's near-zero recovery time is what makes chopping the supply safe.
- Expect roughly 200 mW total dissipation in the transistor (conduction plus switching loss) -- comfortable for a TO-92 package, but if it runs hot, lowering `FAN_PWM_FREQUENCY_HZ` cuts the switching half of that proportionally.

## Important electrical notes

- Same ground-bonding requirement as the Xiao build: do not rely on the display ribbon cable's ground pin as the common ground for the fan-switching circuit. Run a dedicated ground wire from the AD5X mainboard's real power ground to this board's ground bus.
- The 1N5819 flyback diode is required, installed directly across the fan.
- Chopping a 2-wire brushless fan's supply is fan-dependent. This fan is specified as PWM-controllable, but if it stutters, whines, or won't start at low duty, try lowering `FAN_PWM_FREQUENCY_HZ`, raising `FAN_MIN_DUTY_PERCENT`, or setting `FAN_PWM_ENABLED` to `0`.
- As with the Xiao build, an undriven `GPIO5` (at boot, or if the firmware hangs) turns the fan off, not on -- there's no hardware fail-safe defaulting to full speed. The firmware's sensor-fault fail-safe (full speed when the thermistor reading is invalid, ignoring the configured cap) still applies.
- Do not connect 24 V to any board GPIO, `3V3`, or USB-C pin.
