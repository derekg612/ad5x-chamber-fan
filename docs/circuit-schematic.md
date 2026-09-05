# AD5X Chamber Regulator Circuit

This schematic uses the AD5X 24 V auxiliary fan supply, a 4-wire 24 V PWM fan mounted in the back panel, a 2N7000 as an open-drain PWM line driver, the printer display's USB-A port for 5 V/GND to the Xiao ESP32-C3, a 0.96" I2C SSD1306 128x64 OLED, and two momentary buttons for local setpoint adjustment.

The fan has its own PWM control input (25 kHz, open-drain, internally pulled up), so the Xiao does not switch the fan's power directly. The fan is left continuously powered from the fused +24 V rail at the back panel, and the Xiao only pulls the fan's PWM pin low through a small-signal MOSFET to set speed. That PWM wire runs forward from the back panel alongside the display ribbon cable to the Xiao/2N7000. Because the fan's own driver electronics commutate the motor internally, no flyback diode is needed on this signal line.

The Xiao is powered from the printer display's USB-A port (5 V and GND only; D+/D- are unused) instead of a buck converter, since the display port already sits at logic-compatible 5 V.

```text
                         AD5X MAINBOARD AUX FAN PORT (BACK PANEL)
                         (VERIFY POLARITY AND VOLTAGE)

             +24 V  o----[ 1 A fuse ]----+--------------------+
                                         |                    |
                                         |                 +--+---+
                                         |                 | 24 V  |
                                         |                 | PWM   |
                                         |                 | FAN   |
                                         |                 +--+-+--+
                                         |                    | |
             GND   o--------------------+--------------------+ | PWM pin
                                                                |  (internal pull-up)
                                                                |  wire routed forward
                                                                |  alongside display
                                                                |  ribbon cable
                                                                |
                                                          D  +--+--+
                                        Xiao D1 ----[100R]--G  | 2N7000 |
                                                          S  +--+--+
                                                                |
                                        [100k pulldown]         |
                                        gate to source          |
                                                                +---------------- GND

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
| AD5X auxiliary fan `GND` | Fan `GND`, common ground |
| Fan `PWM` pin | 2N7000 drain (wire routed with the display ribbon cable) |
| 2N7000 source | Common ground |
| Xiao `D1` | 100 Ohm gate resistor, then 2N7000 gate |
| 100 kOhm gate pulldown | 2N7000 gate to source/ground |
| Fan `TACH` pin | Not connected (unused) |
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
- Confirm the fan's PWM pin is open-drain with an internal pull-up (standard for 4-wire fans). If it isn't, add an external 4.7-10 kOhm pull-up from the PWM pin to Xiao `3V3`.
- Verify the display USB-A port actually provides 5 V (not 3.3 V or a data-only pass-through) and confirm its GND is common with the AD5X mainboard GND before connecting the Xiao. Confirm the port's available current comfortably covers the Xiao, OLED, and gate-drive current (a few hundred mA is typical and enough); do not add other 5 V loads to this port.
- Do not connect 24 V to any Xiao GPIO, `3V3`, `5V`, or `D+`/`D-` pin.
- The 100 Ohm gate resistor and 100 kOhm gate-to-source pulldown are recommended for defined switching during reset. With the gate pulled low at boot, the 2N7000 is off and the fan's internal pull-up holds the PWM line high, which the fan reads as full speed — this is a hardware fail-safe independent of firmware.
- Keep the 24 V fan wiring physically separate from the thermistor, OLED, and button wiring. Connect all grounds at a deliberate common ground point.
- The OLED and both buttons run at 3.3 V logic; do not substitute a 5 V-only OLED module without level shifting.
- The firmware ramps fan speed proportionally to how far the temperature is above the setpoint, and drives the fan to full speed if the thermistor reading is out of range, so a sensor or wiring fault fails toward ventilation.
