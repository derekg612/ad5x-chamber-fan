# AD5X Chamber Regulator Circuit

This schematic uses the AD5X 24 V auxiliary fan supply, a 4-wire 24 V PWM fan, a 2N7000 as an open-drain PWM line driver, and a buck converter that provides 5 V to the Xiao ESP32-C3.

The fan has its own PWM control input (25 kHz, open-drain, internally pulled up), so the Xiao does not switch the fan's power directly. The fan is left continuously powered from the fused +24 V rail, and the Xiao only pulls the fan's PWM pin low through a small-signal MOSFET to set speed. Because the fan's own driver electronics commutate the motor internally, no flyback diode is needed on this signal line.

```text
                         AD5X MAINBOARD AUX FAN PORT
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
                                                                |
                                                          D  +--+--+
                                        Xiao D1 ----[100R]--G  | 2N7000 |
                                                          S  +--+--+
                                                                |
                                        [100k pulldown]         |
                                        gate to source          |
                                                                +---------------- GND

             +24 V  o----[ buck converter IN+ ]
             GND    o----[ buck converter IN- ]

             Buck OUT+ (regulated 5 V)  o---------- Xiao 5V / VBUS
             Buck OUT- (0 V)            o---------- Xiao GND


                         THERMISTOR INPUT (3.3 V ADC)

             Xiao 3V3 o----[ 10 kOhm NTC, Beta 3950 ]----+----[ 10 kOhm ]----o Xiao GND
                                                         |
                                                         +---------------------- Xiao D0

             Optional: 100 nF capacitor from D0 to Xiao GND, placed near the Xiao.
```

## Connections

| Circuit point | Connect to |
| --- | --- |
| AD5X auxiliary fan `+24 V` | Fuse input, fan `+24 V`, buck `IN+` |
| AD5X auxiliary fan `GND` | Fan `GND`, buck `IN-`, Xiao `GND` |
| Fan `PWM` pin | 2N7000 drain |
| 2N7000 source | Common ground |
| Xiao `D1` | 100 Ohm gate resistor, then 2N7000 gate |
| 100 kOhm gate pulldown | 2N7000 gate to source/ground |
| Fan `TACH` pin | Not connected (unused) |
| Buck converter `OUT+` | Regulated 5 V to Xiao `5V`/`VBUS` pin |
| Buck converter `OUT-` | Xiao `GND` |
| Thermistor divider midpoint | Xiao `D0` |

## Important electrical notes

- Measure the AD5X fan connector with a multimeter before wiring. Confirm it is approximately 24 V DC and identify polarity; do not rely on connector appearance alone.
- Confirm the fan's PWM pin is open-drain with an internal pull-up (standard for 4-wire fans). If it isn't, add an external 4.7-10 kOhm pull-up from the PWM pin to the 5 V buck output.
- Use a buck converter rated for the Xiao and any other 5 V loads. Adjust and measure its output to 5.0 V before connecting the Xiao.
- Do not connect 24 V to any Xiao GPIO, `3V3`, or `5V` pin.
- The 100 Ohm gate resistor and 100 kOhm gate-to-source pulldown are recommended for defined switching during reset. With the gate pulled low at boot, the 2N7000 is off and the fan's internal pull-up holds the PWM line high, which the fan reads as full speed — this is a hardware fail-safe independent of firmware.
- Keep the 24 V fan wiring physically separate from the thermistor wiring. Connect all grounds at a deliberate common ground point.
- The firmware ramps fan speed proportionally to how far the temperature is above the setpoint, and drives the fan to full speed if the thermistor reading is out of range, so a sensor or wiring fault fails toward ventilation.
