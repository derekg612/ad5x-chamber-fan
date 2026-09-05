# AD5X Chamber Regulator Circuit

This schematic uses the AD5X 24 V auxiliary fan supply, a 24 V fan, a logic-level N-channel MOSFET, and a buck converter that provides 5 V to the Xiao ESP32-C3.

```text
                         AD5X MAINBOARD AUX FAN PORT
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
                                         |                    |   diode        |
                                         |                    |                 |
             GND   o--------------------+--------------------+-----------------+--- GND
                                                              |
                                                              |
                                                        D  +--+--+
                                      Xiao D1 ----[100R]--G  | N-MOSFET |  logic-level
                                                        S  +--+--+
                                                              |
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
| AD5X auxiliary fan `+24 V` | Fuse input, fan positive, buck `IN+` |
| AD5X auxiliary fan `GND` | Fan return/MOSFET source, buck `IN-`, Xiao `GND` |
| Fan negative | MOSFET drain |
| MOSFET source | Common ground |
| Xiao `D1` | 100 Ohm gate resistor, then MOSFET gate |
| 100 kOhm gate pulldown | MOSFET gate to source/ground |
| Flyback diode cathode | Fan positive (`+24 V`) |
| Flyback diode anode | Fan negative/MOSFET drain |
| Buck converter `OUT+` | Regulated 5 V to Xiao `5V`/`VBUS` pin |
| Buck converter `OUT-` | Xiao `GND` |
| Thermistor divider midpoint | Xiao `D0` |

## Important electrical notes

- Measure the AD5X fan connector with a multimeter before wiring. Confirm it is approximately 24 V DC and identify polarity; do not rely on connector appearance alone.
- Use a buck converter rated for the Xiao and any other 5 V loads. Adjust and measure its output to 5.0 V before connecting the Xiao.
- Do not connect 24 V to any Xiao GPIO, `3V3`, or `5V` pin.
- Use a logic-level N-MOSFET that is fully enhanced at 3.3 V gate drive and rated above the fan's startup current and 24 V drain voltage. Add a heatsink if needed.
- The 100 Ohm gate resistor and 100 kOhm gate-to-source pulldown are recommended for defined switching during reset.
- The flyback diode must be rated for the fan current and installed across the fan, reverse-biased during normal operation.
- Keep the 24 V fan wiring physically separate from the thermistor wiring. Connect all grounds at a deliberate common ground point.
- The firmware turns the fan on if the thermistor reading is out of range, so a sensor or wiring fault fails toward ventilation.
