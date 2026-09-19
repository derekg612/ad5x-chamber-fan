# AD5X Chamber Regulator

Three interchangeable builds of the same chamber controller, selected by PlatformIO environment:

| Environment | Board | Local UI | Fan control |
| --- | --- | --- | --- |
| `seeed_xiao_esp32c3` | Xiao ESP32-C3 | External 128x64 OLED + 2 buttons | On/off |
| `waveshare_esp32s3_touch_lcd_147` | Waveshare ESP32-S3-Touch-LCD-1.47 | Built-in 172x320 touchscreen | PWM speed control |
| `esp32_c3_oled_042` | Generic ESP32-C3 with 0.42" OLED | Onboard 72x40 OLED + 2 buttons | PWM speed control |

The 0.42" OLED build can also toggle the printer's chamber light over the network -- press both buttons at once.

All three share the same thermistor logic, Wi-Fi setup flow, and web UI. Build/upload a specific one with `pio run -e <environment> -t upload`; the Xiao environment is the default when no `-e` is given.

Wiring docs: [circuit-schematic.md](docs/circuit-schematic.md) (Xiao), [touch-lcd-schematic.md](docs/touch-lcd-schematic.md) (touchscreen), [c3-oled-042-schematic.md](docs/c3-oled-042-schematic.md) (0.42" OLED).

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

## ESP32-C3 OLED 0.42" build (`esp32_c3_oled_042`)

- Generic ESP32-C3 board with an onboard 0.42" 72x40 SSD1306 OLED, powered from the printer display's USB-A port (5 V/GND only)
- 100 kOhm NTC thermistor, Beta 3950
- 100 kOhm fixed resistor
- 2-wire 24 V fan (0.29 A @ 24 V), PWM speed-controlled through a 2N2222A NPN transistor (or BC337) on its ground return, with a 1N5819 Schottky across the fan
- 2 momentary buttons for local setpoint adjustment and printer LED control

Same PWM fan control and `#define` knobs as the touchscreen build, but with a compact portrait display (mounted with the USB port on the left) and physical buttons. Thermistor on `IO2`, transistor base (via 220 Ohm) on `IO20`, and the setpoint up/down buttons on `IO1` and `IO4`. `IO20` is UART0's RX pin, which is free because this environment sends `Serial` over the board's native USB instead. The OLED occupies `IO5`/`IO6` internally (the board reference says `IO8`/`IO9`; see the schematic doc if the panel stays blank).

#### Printer LED toggle

Pressing **both buttons at once** toggles the printer's chamber light and shows the new state on the OLED for a moment. Neither button moves the setpoint while the other is down, and one long two-button press toggles exactly once.

Stock AD5X firmware does not expose Moonraker, so this talks to Flashforge's legacy command socket on TCP 8899, the same protocol as the `ad5x_send_command.py` helper script: commands framed with a leading `~` and CRLF, replies terminated by `ok`. Each toggle takes control (`M601 S1`), sends `M146 r255 g255 b255 F0` or `M146 r0 g0 b0 F0`, then hands control back (`M602`) so the printer's touchscreen is not left locked out. The printer will not report its light state back, so the firmware tracks what it last sent and persists it across reboots; if the light is changed at the printer, the first press afterwards may be a no-op.

The printer's hostname and port are on the web settings page, defaulting to `ad5x.lan:8899`. A failed toggle leaves the setting untouched and shows `fail` on the panel, with the detail on the serial monitor.

### Enclosure

[hardware/generate_c3_oled_case.py](hardware/generate_c3_oled_case.py) generates a two-part snap-fit case for this build:

```
~/.pythonenvs/stl/bin/python3 hardware/generate_c3_oled_case.py
```

It writes `case_bottom.stl` and `case_top.stl` alongside the script. The board is held without soldered headers, resting on a perimeter ledge and pressed down by pads on the lid, with 7 mm of headroom so the 2N2222A can lie flat on the PCB. Openings: USB-C on one end, a 6 mm circular cable hole in the middle of the tray floor -- the back panel -- for the fan return, ground bond, thermistor pair and button wires, and a window over the OLED. That hole opens into the relief under the board, so those wires solder to pads on the PCB's underside and drop straight out the back; there is only 1.2 mm of height in there, so they cannot run sideways first. Every dimension is a named constant at the top of the script, and `check_parameters()` asserts the ones that quietly ruin a print (wall left behind the snap groove, snap deflection, material bridging the wall openings, TO-92 headroom).

[hardware/preview_case.py](hardware/preview_case.py) renders the result for inspection:

```
~/.pythonenvs/stl/bin/python3 hardware/preview_case.py
```

It writes `preview_iso.png` (shaded views of each part plus an exploded view) and `preview_sections.png` (cuts through the assembled case). The section views are the useful ones -- they show the lip seated in the rebate, the snap ridge in its groove, the PCB on its ledge, and the material bridging the wall openings. Requires `matplotlib` in the same environment.
