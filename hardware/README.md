# Circuit board (KiCad 10)

A single two-layer carrier board. The Feather ESP32-S3 plugs into two female headers (J1, J2). The microSD, I2S microphone and I2S amplifier breakouts plug into sockets (J3, J4, J5). The board also carries an LED resistor and header (J6), a 4-pin LCD socket (J7), a 3-pin mode toggle header (J8), a 2-pin rotary phone dial header (J9) and a 3-pin amp power jumper (JP1). It is 72 x 86 mm, with ground planes on both layers.

| File | What it is |
|---|---|
| `supersecretgossipphone.kicad_sch` | Schematic |
| `supersecretgossipphone.kicad_pcb` | Board layout |
| `schematic.pdf`, `pcb_layout.pdf`, `pcb_top.png` | Previews |
| `fabrication_gerbers.zip` | Gerbers and drill files for a board house |

## Pin map (matches `mp3_recorder.ino`)

| Function | Feather pin |
|---|---|
| microSD chip select | D10 (SCK, MOSI, MISO for data) |
| I2S bit clock / word select | D9 / D6 |
| Microphone data in / amp data out | D12 / D5 |
| Rotary phone dial pulse contact | A3 (other wire to GND) |
| Mode toggle | D13 (3V = RECORD, GND = LISTEN) |
| LCD | SDA, SCL, and 5 V from USB |
| Recording LED (external, through R1 330 ohm) | D11 |

## Wiring notes

- **JP1** picks the amp supply: shunt pins 1-2 for USB 5 V (louder) or 2-3 for 3.3 V.
- **J6** is the external LED: pin 1 is the anode (after R1), pin 2 is ground.
- **J8** is the mode toggle: the middle pin is the common terminal; one end goes to GND (LISTEN), the other to 3V (RECORD).
- **J9** is the phone dial's pulse contact: pin 1 is the pulse wire (to A3), pin 2 is GND.
- **J7** order is GND, VCC, SDA, SCL, which is common but not universal. Check your LCD backpack.
- The microphone's SEL pin is tied to GND (left channel).

## Not yet verified

- The board has passed KiCad's design rule check (no errors, nothing unrouted) and the schematic passes ERC apart from three harmless label-name warnings. It has **not** been built or tested.
- The Feather header positions come from Adafruit's Feather specification: the 16-pin row is on the bottom edge when USB is on the left, the 12-pin row is on top, 0.8 in apart, and the two rows end at the same point so BAT sits opposite A0. Check this against your board before ordering.
- Breakout pin orders follow the Adafruit boards (microSD 254, SPH0645 and MAX98357A). Check them against the exact boards you buy.
- The dial's pulse contact polarity and timing depend on the phone. The sketch assumes the contact is closed at rest and opens once per pulse; if your dial behaves differently, the constants at the top of the sketch may need adjusting.

## Regenerating

`tools/` holds the Python scripts that generate both files from one pin list. They need KiCad 10 installed (see the top of each script).
