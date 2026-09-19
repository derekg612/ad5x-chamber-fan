# ESP32-C3 OLED 0.42" Build

A generic ESP32-C3 board with an onboard 0.42" 72x40 SSD1306 OLED
([board reference](https://www.espboards.dev/esp32/esp32-c3-oled-042/)), with two external momentary buttons for setpoint adjustment. The fan circuit is identical to the touchscreen build -- 2N2222A (or BC337) low-side switch, 220 Ohm base resistor, 1N5819 Schottky, PWM speed control -- so see [touch-lcd-schematic.md](touch-lcd-schematic.md) for the component sizing rationale and [circuit-schematic.md](circuit-schematic.md) for the ground-bonding requirement. Both apply here unchanged.

## Pin budget

This board is unusually tight: of its 13 broken-out GPIOs, only IO0, IO1, IO3 and IO10 are free of strapping, JTAG or UART duty. The build uses:

| GPIO | Use | Notes |
| --- | --- | --- |
| IO1 | Setpoint-up button | Internal pull-up |
| IO2 | Thermistor divider midpoint | ADC1_CH2. Strapping pin -- see below. |
| IO4 | Setpoint-down button | Internal pull-up. JTAG TMS, free because JTAG goes over USB. |
| IO20 | 2N2222A base (via 220 Ohm) | PWM output. UART0 RX, free because Serial goes over USB. |

Reserved, do not reuse:

| GPIO | Function |
| --- | --- |
| IO5 | OLED I2C SDA (also JTAG TDI) |
| IO6 | OLED I2C SCL (also JTAG TCK) |
| IO8, IO9 | Strapping -- IO9 selects download mode at reset |
| IO18, IO19 | Native USB (the USB-C port) |

IO0, IO3, IO7, IO10 and IO21 are unused.

The board reference puts the OLED on IO8/IO9, but on the board this was built with it is wired to IO5/IO6, and boards sold under this name vary. If the panel stays blank, set `OLED_SDA_PIN`/`OLED_SCL_PIN` in `src/main_c3oled.cpp` to 8/9 and try again.

IO4, IO5 and IO6 being JTAG pins is harmless: the C3 routes JTAG through its built-in USB by default, so they behave as ordinary GPIO. IO8/IO9 are left unused, but they are still strapping pins, so don't add anything there that could pull them low at reset.

**Thermistor on IO2:** IO2 is listed as a strapping pin, but per Espressif's [hardware design guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c3/schematic-checklist.html) it does not actually select the boot mode -- they only recommend pulling it high to guard against glitches. The divider holds it partway instead (about 1.65 V at 25 C, rising as the chamber warms), so this should be fine, but if boots are ever unreliable, move the thermistor to IO3 (also ADC1) and change `THERMISTOR_PIN`.

**Fan on IO20:** the USB-C port is wired to the C3's native USB, so flashing and the serial monitor never touch UART0 (IO20/IO21). The `esp32_c3_oled_042` environment sets `ARDUINO_USB_CDC_ON_BOOT=1` so that `Serial` also goes over USB, which leaves IO20 free. IO20 was picked over IO21 because it is an input during boot: IO21 is UART0 TX, which idles high and carries the boot log at every reset, so the fan would run and chatter for a moment before the firmware takes over. If the fan still twitches at power-up on IO20, add a 4.7-10 kOhm resistor from IO20 to GND.

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
                                         |          IO20 --[220R]-------B  |2N2222A|
                                         |                              E  +---+---+
                                         |                                    |
             GND   o--------------------+------------------------------------+--- GND

             2N2222A (TO-92): E-B-C left to right, flat face toward you.

                         THERMISTOR INPUT (3.3 V ADC)

             3V3 o----[ 100 kOhm NTC, Beta 3950 ]----+----[ 100 kOhm ]----o GND
                                                      |
                                                      +--------------------- IO2

                         SETPOINT BUTTONS (momentary, to GND)

             IO1 o----[ momentary button ]---- GND     (setpoint up)
             IO4 o----[ momentary button ]---- GND     (setpoint down)
             Internal pull-ups enabled in firmware; no external resistors needed.

                         POWER

             Printer display USB-A +5 V  o------- board 5 V / VBUS
             Printer display USB-A GND   o------- board GND
```

## Display

The board is mounted in portrait with the USB port on the left and the screen facing you, so the 72x40 panel is drawn a quarter turn round as 40 pixels wide by 72 tall:

```
  NOW
  32.4
  SET
  35.0
  FAN
  45%
192.168.
  1.42
```

The current temperature is the largest reading (`fault` if the thermistor reading is invalid). Below it are the setpoint and the fan duty (`idle` when off), each with a tiny label above its value, since 40 pixels is only six characters wide in the value font. The IP address is split after its second octet so that even `255.255.255.255` fits.

Pressing both buttons shows `LIGHT` over `...` while the command is sent, then `ON`, `OFF`, or `fail` if the printer didn't reply.

If the text comes out upside down, the panel is mounted the other way round on your board: change `U8G2_R3` to `U8G2_R1` in the display constructor in `src/main_c3oled.cpp`.

## Important electrical notes

- Same ground-bonding requirement as the other builds: run a dedicated ground wire from the AD5X mainboard's real power ground to this board's ground bus. Do not rely on the display ribbon cable's ground pin.
- The 1N5819 flyback diode is required, installed directly across the fan.
- The 100 kOhm divider has a source impedance of around 50 kOhm, which is high enough for the ADC's sampling capacitor and fan-switching noise to pull readings around. A 100 nF ceramic capacitor from IO2 to GND, placed at the board, steadies it.
- An undriven IO20 (at boot, or if the firmware hangs) turns the fan off, not on. The firmware's sensor-fault fail-safe (full speed when the thermistor reading is invalid) still applies.
- Do not connect 24 V to any board GPIO, `3V3`, or `5V` pin.
