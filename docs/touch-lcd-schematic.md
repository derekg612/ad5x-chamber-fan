# Waveshare ESP32-S3-Touch-LCD-1.47 Build

This variant replaces the Xiao's external OLED and two physical buttons with the Waveshare ESP32-S3-Touch-LCD-1.47's built-in 172x320 capacitive touchscreen. The fan/thermistor circuit is electrically the same as the Xiao build (BC337 low-side switch, flyback diode, dedicated ground bonding wire) -- only the controller board and its pin numbers change. See [circuit-schematic.md](circuit-schematic.md) for the fan-switching rationale and the ground-bonding warning; both apply here unchanged.

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
                                         |                              C  +--+--+
                                         |         GPIO5 --[330R]--------B  | BC337 |
                                         |                              E  +--+--+
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
| Fan `GND` (motor return) | BC337 collector |
| BC337 emitter | Common ground |
| Board `GPIO5` | 330 Ohm base resistor, then BC337 base |
| Flyback diode cathode | Fan `+24 V` |
| Flyback diode anode | Fan `GND` / BC337 collector |
| Thermistor divider midpoint | Board `GPIO4` |
| Printer display USB-A `+5 V` / `GND` | Board's own USB-C connector |

## UI

The screen runs in portrait orientation (172 wide x 320 tall) with the USB-C port at the bottom. It shows the current temperature, the setpoint, and fan state, with two large on-screen buttons at the bottom of the screen (`-` and `+`) that nudge the setpoint by 0.5 degree C per tap -- the same step size, debounce behavior, and NVS persistence as the physical buttons in the Xiao build. The web UI (same page/settings routes as the Xiao build) is also still available over Wi-Fi.

**Orientation is a firmware assumption, not a confirmed fact** -- `DISPLAY_ROTATION` in `src/main_s3touch.cpp` is set to `2` based on the vendor demo's default (`0`) placing the USB-C at the top; rotating 180 degrees should put it at the bottom. Verify this on first boot: if the UI renders upside down, change `DISPLAY_ROTATION` to `0` and re-flash. The touch driver is initialized with the same rotation value so touch coordinates stay aligned with what's drawn.

## Important electrical notes

- Same ground-bonding requirement as the Xiao build: do not rely on the display ribbon cable's ground pin as the common ground for the fan-switching circuit. Run a dedicated ground wire from the AD5X mainboard's real power ground to this board's ground bus.
- Same BC337 base-resistor guidance as the Xiao build: 330 Ohm is sized for typical small-fan currents (100-300 mA); drop toward 220 Ohm if your fan draws more.
- The flyback diode is required, installed directly across the fan.
- As with the Xiao build, an undriven `GPIO5` (at boot, or if the firmware hangs) turns the fan off, not on -- there's no hardware fail-safe defaulting to full speed. The firmware's sensor-fault fail-safe (fan on when the thermistor reading is invalid) still applies.
- Do not connect 24 V to any board GPIO, `3V3`, or USB-C pin.
