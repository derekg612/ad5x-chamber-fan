# AD5X Chamber Regulator

## Hardware

- Xiao ESP32-C3
- 10 kOhm NTC thermistor, Beta 3950
- 10 kOhm fixed resistor
- 5 V fan controlled through a logic-level N-MOSFET, with a flyback diode

Wire the thermistor and fixed resistor as a divider from 3.3 V to GND, with the midpoint on `D0`. The firmware assumes the thermistor is the high-side component and the fixed resistor is the low-side component. Connect the MOSFET gate to `D1`, share grounds, and power the fan from an appropriate external supply. Do not power the fan through the Xiao.

On first boot, connect to the `AD5X-Chamber-Setup` Wi-Fi network using password `chamber123`, then follow the setup page to select the printer's Wi-Fi network. The saved connection is reused on later boots. Open the IP address printed to the serial monitor at 115200 baud. The chamber settings are saved in nonvolatile storage.

The fan turns on at `setpoint + upper threshold` and turns off at `setpoint - lower threshold`. A disconnected or out-of-range thermistor turns the fan on as a fail-safe.