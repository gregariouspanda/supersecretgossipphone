# Super Secret Gossip Phone

An ESP32 audio recorder and player. It records from a microphone to MP3 files on a microSD card, then plays them back through a small speaker. It is built for an Adafruit Feather ESP32-S3 and is written for the Arduino IDE.

> Status: the code has not been compiled or tested on hardware. Pin numbers and library header paths may need small tweaks.

## Parts list

| Item | Notes |
|---|---|
| Adafruit Feather ESP32-S3 | Any ESP32-S3 or ESP32 Feather with enough speed for software MP3 encoding. A SAMD (M0/M4) Feather is too slow. |
| microSD card breakout (3V-compatible) | Plus a microSD card, 32 GB or smaller, formatted FAT32. |
| I2S MEMS microphone breakout | SPH0645, ICS-43434, or INMP441. |
| I2S amplifier breakout | MAX98357A (3 W class-D). |
| Speaker | 4 to 8 ohm, around 3 W. |
| Rotary phone dial | An old telephone dial with a pulse contact (two wires). Dialing a digit opens the contact that many times (ten pulses for 0). |
| 20x4 I2C character LCD (HD44780 with PCF8574 backpack) | Shows the file list, the selected recording and record status. Default I2C address 0x27 (some are 0x3F). |
| On/on SPDT toggle switch | Switches between RECORD and LISTEN mode. |
| 1 external LED (any color, 3 or 5 mm) and a 330 ohm resistor | Recording indicator. The carrier board has the resistor and a 2-pin header, so the LED can sit on your enclosure on short wires. |
| LiPo battery, 3.7 V (optional) | Plugs into the Feather's JST connector for portable use. |
| Breadboard and jumper wires, or a perfboard | Header pins for the Feather and breakouts. |

## Wiring

The microphone and amplifier share the I2S clock lines (BCLK and WS) and use separate data pins. All pins are defined at the top of `mp3_recorder.ino`.

### microSD breakout

| SD breakout | Feather |
|---|---|
| 3V / VCC | 3V |
| GND | GND |
| CLK | SCK |
| DI / MOSI | MOSI |
| DO / MISO | MISO |
| CS | D10 |

### I2S microphone

| Mic | Feather |
|---|---|
| 3V / VDD | 3V |
| GND | GND |
| BCLK | D9 |
| LRCL / WS | D6 |
| DOUT | D12 |
| SEL / L/R | GND (left channel; match the code if you change it) |

### I2S amplifier (MAX98357A)

| Amp | Feather |
|---|---|
| VIN | USB (5 V) for more volume, or 3V |
| GND | GND |
| BCLK | D9 |
| LRC | D6 |
| DIN | D5 |
| Speaker +/- | Speaker |

### Rotary phone dial

Use the pulse contact, the pair of wires that opens and closes as the dial returns home. Which wires those are depends on the phone, so test with a continuity meter: the pair that is closed at rest and clicks open and closed while the dial spins back. The other contacts (the off-normal switch, the hook switch and the bell) are not used.

| Dial | Feather |
|---|---|
| Pulse contact, one wire | A3 |
| Pulse contact, other wire | GND |

### Recording LED

| LED | Feather |
|---|---|
| Anode (+, long leg) through a 330 ohm resistor | D11 |
| Cathode (-, short leg) | GND |

The LED is external and uses D11, so the Feather's own D13 LED stays unused.

### LCD (I2C)

| LCD backpack | Feather |
|---|---|
| GND | GND |
| VCC | USB (5 V) |
| SDA | SDA |
| SCL | SCL |

> Most PCF8574 backpacks pull SDA and SCL up to their own supply. With the LCD on 5 V that puts 5 V on the ESP32's 3.3 V pins. Cut or remove the backpack's pull-up resistors (the Feather already has I2C pull-ups to 3.3 V), or use a 3.3 V LCD.

### Mode toggle (on/on SPDT)

| Toggle | Feather |
|---|---|
| Common (middle) | D13 |
| One throw (LISTEN) | GND |
| Other throw (RECORD) | 3V |

## Software setup

1. Install the Arduino IDE and the **esp32 by Espressif** board package. Select the Adafruit Feather ESP32-S3 board.
2. Install these three libraries from pschatzmann (Library Manager or GitHub):
   - `arduino-audio-tools`
   - `arduino-libshine` (MP3 encoder)
   - `arduino-libhelix` (MP3 decoder)
   - Also install **LiquidCrystal I2C** (Frank de Brabander) for the LCD.
3. Open `mp3_recorder/mp3_recorder.ino`, then compile and upload.
4. Open the Serial Monitor at 115200 baud to see status messages.

## Usage

Flip the **mode toggle** to RECORD or LISTEN, then use the dial.

**RECORD mode**

| Dial | What it does |
|---|---|
| 9 | Record. Dial 9 again to stop. Recording also stops by itself after 5 minutes. Files are saved as `REC001.mp3`, `REC002.mp3`, and so on. |
| 8 | Keep. Locks in the recording you just made so it can no longer be replaced. |
| 5 | Delete and re-record. Within 1 minute after a recording ends, this deletes that recording and records over it under the same name. Only the most recent recording can be replaced. |

After a recording ends the LCD shows how many seconds are left to keep or re-record it. Dialing 9 instead starts a new file and leaves the previous one as it is. After the minute is up, or after a reset or power cycle, the old recording can no longer be replaced.

**LISTEN mode:** dial the number of the recording you want. Dial 5 for `REC005.mp3`, or 1 then 2 for `REC012.mp3` (up to three digits). The LCD shows the number and file name as you dial, and it plays about 2 seconds after the last digit. Dial any digit while it plays to stop it.

Flipping the toggle while recording or playing stops it.

**Recording LED:** it is on while recording. At the 1 minute mark it goes dark for 2 seconds, then comes back on. For the last 30 seconds (4:30 to 5:00) it blinks once per second until recording stops. It is off when not recording.

## Troubleshooting

- **"SD card init failed":** check the wiring, the FAT32 format, and the CS pin.
- **Silent or noisy recordings:** check the mic's SEL pin, the 3.3 V supply, and that the I2S pins match the code.
- **No playback sound:** check the amp's wiring and speaker polarity. Power the amp from 5 V for more volume.
- **Choppy audio:** use a faster SD card, or lower `SAMPLE_RATE` in the sketch (for example, 22050).
- **Compile errors about codec headers:** library versions differ. Adjust the `#include "AudioTools/AudioCodecs/..."` lines to match the installed version.

## Circuit board

The `hardware/` folder has a KiCad 10 schematic and a 72 x 86 mm two-layer carrier board. The Feather and the three breakouts plug into sockets, and the phone dial, mode toggle, LED and LCD connect to it. See [hardware/README.md](hardware/README.md).
