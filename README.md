# AD5X Chamber Regulator

Two interchangeable builds of the same chamber controller, selected by PlatformIO environment:

- `seeed_xiao_esp32c3` -- Xiao ESP32-C3 with an external OLED and two physical buttons.
- `waveshare_esp32s3_touch_lcd_147` -- Waveshare ESP32-S3-Touch-LCD-1.47, using its built-in touchscreen instead of an OLED and buttons.

Both share the same fan-control and thermistor logic, Wi-Fi setup flow, and web UI. Build/upload a specific one with `pio run -e <environment> -t upload`; the Xiao environment is the default when no `-e` is given.

See [docs/circuit-schematic.md](docs/circuit-schematic.md) for the Xiao build's wiring and [docs/touch-lcd-schematic.md](docs/touch-lcd-schematic.md) for the touchscreen build's wiring.

## Common behavior

On first boot, connect to the `AD5X-Chamber-Setup` Wi-Fi network using password `chamber123`, then follow the setup page to select the printer's Wi-Fi network. The saved connection is reused on later boots. Open the IP address printed to the serial monitor at 115200 baud, or shown on the display. Settings are saved in nonvolatile storage.

The fan turns on at `setpoint + upper threshold` and turns off at `setpoint - lower threshold`. A disconnected or out-of-range thermistor turns the fan on as a fail-safe.

## Xiao ESP32-C3 build (`seeed_xiao_esp32c3`)

- Xiao ESP32-C3, powered from the printer display's USB-A port (5 V/GND only)
- 10 kOhm NTC thermistor, Beta 3950
- 10 kOhm fixed resistor
- 24 V fan (no PWM input) mounted in the back panel, switched on/off through a 2N2222A NPN transistor (or BC337) on its ground return (return wire routed forward with the display ribbon cable), with a flyback diode across the fan
- 0.96" I2C SSD1306 128x64 OLED display
- 2 momentary buttons for local setpoint adjustment

Wire the thermistor and fixed resistor as a divider from 3.3 V to GND, with the midpoint on `D0`. The firmware assumes the thermistor is the high-side component and the fixed resistor is the low-side component. Connect the 2N2222A base to `D1` through a 330 Ohm resistor, share grounds, and power the fan continuously from an appropriate external 24 V supply. Do not power the fan through the Xiao.

Connect the OLED over I2C to `D4` (SDA) and `D5` (SCL), powered from `3V3`. Wire the two momentary buttons between `D2`/`D3` and `GND`; the firmware uses internal pull-ups, so no external resistors are needed. One button raises the setpoint, the other lowers it, in 0.5 degree C steps, with the change reflected immediately on the OLED and saved to nonvolatile storage.

## Waveshare ESP32-S3-Touch-LCD-1.47 build (`waveshare_esp32s3_touch_lcd_147`)

- Waveshare ESP32-S3-Touch-LCD-1.47 (built-in 172x320 touchscreen), powered from the printer display's USB-A port via its own USB-C port (5 V/GND only)
- 10 kOhm NTC thermistor, Beta 3950
- 10 kOhm fixed resistor
- 2-wire 24 V fan (0.29 A @ 24 V) mounted in the back panel, PWM speed-controlled through a 2N2222A NPN transistor (or BC337) on its ground return, with a 1N5819 Schottky across the fan

There's no separate OLED or physical buttons in this build -- the board's own display and capacitive touchscreen replace both. The UI runs in portrait orientation with the USB-C port at the bottom of the screen; two on-screen `-`/`+` buttons adjust the setpoint by 0.5 degree C per tap, the same step size and persistence behavior as the button build. Wire the thermistor to `GPIO4` and the 2N2222A base (through a 220 Ohm resistor) to `GPIO5`.

Unlike the Xiao build's on/off control, fan speed ramps proportionally between `setpoint - lower threshold` (idle) and `setpoint + upper threshold` (the configured maximum), with a duty floor so the fan doesn't sit below its stall speed. A maximum-fan-speed cap is available on the web settings page. All the fan drive parameters -- PWM frequency, duty floor, and an on/off fallback -- are `#define`s at the top of [src/main_s3touch.cpp](src/main_s3touch.cpp). See [docs/touch-lcd-schematic.md](docs/touch-lcd-schematic.md) for the full pinout, component sizing rationale, and the display/touch pins that are reserved internally.
