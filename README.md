# AD5X Chamber Regulator

See [docs/circuit-schematic.md](docs/circuit-schematic.md) for the 24 V PWM fan, 2N7000 PWM driver, buck converter, Xiao, and thermistor wiring.

## Hardware

- Xiao ESP32-C3
- 10 kOhm NTC thermistor, Beta 3950
- 10 kOhm fixed resistor
- 24 V 4-wire PWM fan, with its PWM control pin driven through a 2N7000 open-drain switch

Wire the thermistor and fixed resistor as a divider from 3.3 V to GND, with the midpoint on `D0`. The firmware assumes the thermistor is the high-side component and the fixed resistor is the low-side component. Connect the 2N7000 gate to `D1` through a 100 Ohm resistor, share grounds, and power the fan continuously from an appropriate external 24 V supply. Do not power the fan through the Xiao.

On first boot, connect to the `AD5X-Chamber-Setup` Wi-Fi network using password `chamber123`, then follow the setup page to select the printer's Wi-Fi network. The saved connection is reused on later boots. Open the IP address printed to the serial monitor at 115200 baud. The chamber settings are saved in nonvolatile storage.

Fan speed ramps proportionally between `setpoint - lower threshold` (idle) and `setpoint + upper threshold` (the configured maximum fan speed), rather than switching fully on/off. A disconnected or out-of-range thermistor drives the fan to full speed as a fail-safe, regardless of the configured maximum.