# ESP32-C3 OLED 0.42" Build

A generic ESP32-C3 board with an onboard 0.42" 72x40 SSD1306 OLED
([board reference](https://www.espboards.dev/esp32/esp32-c3-oled-042/)), with two external momentary buttons for setpoint adjustment. The fan circuit is identical to the touchscreen build -- 2N2222A (or BC337) low-side switch, 220 Ohm base resistor, 1N5819 Schottky, PWM speed control -- so see [touch-lcd-schematic.md](touch-lcd-schematic.md) for the component sizing rationale and [circuit-schematic.md](circuit-schematic.md) for the ground-bonding requirement. Both apply here unchanged.

## Pin budget

This board is unusually tight. Of its 13 broken-out GPIOs, only **IO0, IO1, IO3 and IO10** are free of strapping, JTAG, or UART duty -- and all four are used:

| GPIO | Use | Notes |
| --- | --- | --- |
| IO0 | Setpoint-up button | ADC1_CH0. See the onboard-LED caveat below. |
| IO1 | Thermistor divider midpoint | ADC1_CH1 |
| IO3 | Setpoint-down button | ADC1_CH3 |
| IO10 | 2N2222A base (via 220 Ohm) | PWM output |

Reserved, do not reuse:

| GPIO | Function |
| --- | --- |
| IO8 | OLED I2C SDA (also a strapping pin -- must be high at reset) |
| IO9 | OLED I2C SCL (also the boot-mode strapping pin) |
| IO2, IO4-IO7 | Strapping / JTAG |
| IO20, IO21 | UART0 console -- repurposing these breaks serial programming |

The OLED sharing IO8/IO9 with strapping pins works because the I2C pull-ups hold both in the states the bootloader wants. It does mean you cannot put anything else on that bus without checking it doesn't disturb boot.

## Onboard LED caveat on IO0

Board references list an onboard LED on IO0, which is also where the setpoint-up button goes. Whether that's a problem depends on how the LED is wired, and this has not been verified on hardware:

- **LED wired from 3.3 V through a resistor to IO0** (active-low): no problem. The internal pull-up holds the pin high, the button pulls it low, and the LED lights when pressed as a free indicator.
- **LED wired from IO0 through a resistor to ground** (active-high): a problem. The LED clamps the pin below the input-high threshold, so the button reads as permanently pressed and the setpoint will run away upward.

**Symptom to watch for on first boot:** the setpoint climbing on its own without touching anything. If that happens, swap the roles of IO0 and IO1 in `src/main_c3oled.cpp` -- put the thermistor on IO0 (still ADC-capable) and the up button on IO1. That's a two-line change to `THERMISTOR_PIN` and `BUTTON_UP_PIN`, and keeps everything on the four safe pins.

## Wiring

```text
                         AD5X MAINBOARD AUX FAN PORT (BACK PANEL)

             +24 V  o----[ 1 A fuse ]----+--------------------+
                                         |                    |
                                         |                 +--+--+
                                         |                 | 24 V |
                                         |                 | FAN  |
                                         |                 +--+--+
                                         |                    |
                                         |                    +------|<|------+
                                         |                    |    1N5819     |
                                         |                    |               |
                                         |                              C  +---+---+
                                         |          IO10 --[220R]-------B  |2N2222A|
                                         |                              E  +---+---+
                                         |                                    |
             GND   o--------------------+------------------------------------+--- GND

             2N2222A (TO-92): E-B-C left to right, flat face toward you.

                         THERMISTOR INPUT (3.3 V ADC)

             3V3 o----[ 10 kOhm NTC, Beta 3950 ]----+----[ 10 kOhm ]----o GND
                                                     |
                                                     +---------------------- IO1

                         SETPOINT BUTTONS (momentary, to GND)

             IO0 o----[ momentary button ]---- GND     (setpoint up)
             IO3 o----[ momentary button ]---- GND     (setpoint down)
             Internal pull-ups enabled in firmware; no external resistors needed.

                         POWER

             Printer display USB-A +5 V  o------- board 5 V / VBUS
             Printer display USB-A GND   o------- board GND
```

## Display

The 72x40 panel is small enough that the layout is four tight lines rather than the roomier one on the 128x64 OLED build:

```
Set 35.0
Now 32.4
Fan 45%
192.168.1.42
```

Setpoint, current temperature (or `Now fault`), fan duty (or `Fan idle`), and the IP address in a smaller font. A long IP may clip slightly at the right edge.

## Important electrical notes

- Same ground-bonding requirement as the other builds: run a dedicated ground wire from the AD5X mainboard's real power ground to this board's ground bus. Do not rely on the display ribbon cable's ground pin.
- The 1N5819 flyback diode is required, installed directly across the fan.
- An undriven IO10 (at boot, or if the firmware hangs) turns the fan off, not on. The firmware's sensor-fault fail-safe (full speed when the thermistor reading is invalid) still applies.
- Do not connect 24 V to any board GPIO, `3V3`, or `5V` pin.
