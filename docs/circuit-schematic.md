# AD5X Chamber Regulator Circuit

This schematic uses the AD5X 24 V auxiliary fan supply, a plain (non-PWM) 24 V fan mounted in the back panel, a BC337 NPN transistor as an on/off low-side switch, the printer display's USB-A port for 5 V/GND to the Xiao ESP32-C3, a 0.96" I2C SSD1306 128x64 OLED, and two momentary buttons for local setpoint adjustment.

The fan has no speed control input, so the Xiao switches it fully on or off instead of ramping speed. The fan's `+24 V` lead stays at the back panel, fused. The fan's `GND` return is run forward from the back panel (alongside the display ribbon cable) to the Xiao's control area, where the BC337 interrupts it to switch the fan. Because the BC337 is now switching real motor current, a flyback diode across the fan is required again to clamp the inductive kick when the transistor turns off.

The Xiao is powered from the printer display's USB-A port (5 V and GND only; D+/D- are unused) instead of a buck converter, since the display port already sits at logic-compatible 5 V.

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
                                         |                    |  alongside display
                                         |                    |  ribbon cable
                                         |                    |               |
                                         |                              C  +--+--+
                                         |            Xiao D1 --[330R]--B  | BC337 |
                                         |                              E  +--+--+
                                         |                                    |
             GND   o--------------------+------------------------------------+--- GND

                         PRINTER DISPLAY USB-A PORT

             USB-A +5 V  o---------------------------------- Xiao 5V / VBUS
             USB-A GND   o---------------------------------- Xiao GND
             (D+/D- not connected)


                         THERMISTOR INPUT (3.3 V ADC)

             Xiao 3V3 o----[ 10 kOhm NTC, Beta 3950 ]----+----[ 10 kOhm ]----o Xiao GND
                                                         |
                                                         +---------------------- Xiao D0

             Optional: 100 nF capacitor from D0 to Xiao GND, placed near the Xiao.


                         0.96" I2C SSD1306 128x64 OLED

             OLED VCC  o---------------------------------- Xiao 3V3
             OLED GND  o---------------------------------- Xiao GND
             OLED SDA  o---------------------------------- Xiao D4
             OLED SCL  o---------------------------------- Xiao D5


                         SETPOINT BUTTONS (momentary, to GND)

             Xiao D2 o----[ momentary button ]---- GND     (setpoint up)
             Xiao D3 o----[ momentary button ]---- GND     (setpoint down)
             Internal pull-ups enabled in firmware; no external resistors needed.
```

## Connections

| Circuit point | Connect to |
| --- | --- |
| AD5X auxiliary fan `+24 V` | Fuse input, fan `+24 V` |
| Fan `GND` (motor return) | BC337 collector (wire routed with the display ribbon cable) |
| BC337 emitter | Common ground |
| Xiao `D1` | 330 Ohm base resistor, then BC337 base |
| Flyback diode cathode | Fan `+24 V` |
| Flyback diode anode | Fan `GND` / BC337 collector |
| Printer display USB-A `+5 V` | Xiao `5V`/`VBUS` pin |
| Printer display USB-A `GND` | Xiao `GND` |
| Thermistor divider midpoint | Xiao `D0` |
| OLED `VCC` | Xiao `3V3` |
| OLED `GND` | Xiao `GND` |
| OLED `SDA` | Xiao `D4` |
| OLED `SCL` | Xiao `D5` |
| Setpoint-up button | Xiao `D2` to `GND` |
| Setpoint-down button | Xiao `D3` to `GND` |

## Important electrical notes

- Measure the AD5X fan connector with a multimeter before wiring. Confirm it is approximately 24 V DC and identify polarity; do not rely on connector appearance alone.
- The BC337 is a plain NPN switch, not a logic-level MOSFET, so there's no gate-threshold uncertainty at 3.3 V — the 330 Ohm base resistor gives a forced beta of roughly 10-20 at typical small-fan currents (100-300 mA), which is enough to saturate it reliably. If your fan draws noticeably more than that, drop the base resistor toward 220 Ohm for more base drive margin.
- The flyback diode must be rated for the fan's current and installed directly across the fan, reverse-biased during normal operation. This is required now that the BC337 switches real motor current, not just a logic signal.
- Verify the display USB-A port actually provides 5 V (not 3.3 V or a data-only pass-through). Do not add other 5 V loads to this port beyond the Xiao and OLED.
- **Do not rely on the display ribbon cable's ground pin as the system's common ground.** Measured continuity between the USB-A GND and the AD5X mainboard ground through the display cable can be tens of ohms (a thin signal-return trace, not a real ground bond) — nowhere near low enough to carry the fan's switched current or hold a stable ADC reference. Run a dedicated ground wire, comparable gauge to the fan wiring, directly from the AD5X mainboard's real power ground (the same GND the aux fan port uses) forward to the Xiao's ground bus, where it ties together the BC337 emitter, thermistor return, and OLED GND. That wire's low resistance dominates over the weak ribbon-cable path and establishes the actual common ground the circuit needs; the USB-A port then only needs to supply `5V`.
- Do not connect 24 V to any Xiao GPIO, `3V3`, `5V`, or `D+`/`D-` pin.
- Keep the 24 V fan wiring physically separate from the thermistor, OLED, and button wiring.
- The OLED and both buttons run at 3.3 V logic; do not substitute a 5 V-only OLED module without level shifting.
- Unlike the earlier PWM design, an undriven `D1` (GPIO low, at boot or if the firmware hangs) turns the BC337 off, which turns the fan off — there is no hardware fail-safe defaulting to full speed. The firmware's fail-safe (forcing the fan on when the thermistor reading is invalid) only covers sensor faults, not a hung MCU. The firmware turns the fan on if the thermistor reading is out of range, so a sensor or wiring fault fails toward ventilation.
