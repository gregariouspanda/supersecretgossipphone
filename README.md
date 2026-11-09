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
| 3 momentary push buttons | Record, Play/Stop, Next. |
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

### Buttons

Wire one leg of each button to the pin and the other to GND. The sketch enables the internal pull-ups.

| Button | Feather |
|---|---|
| Record / Stop | A0 |
| Play / Stop | A1 |
| Next file | A2 |

## Software setup

1. Install the Arduino IDE and the **esp32 by Espressif** board package. Select the Adafruit Feather ESP32-S3 board.
2. Install these three libraries from pschatzmann (Library Manager or GitHub):
   - `arduino-audio-tools`
   - `arduino-libshine` (MP3 encoder)
   - `arduino-libhelix` (MP3 decoder)
3. Open `mp3_recorder/mp3_recorder.ino`, then compile and upload.
4. Open the Serial Monitor at 115200 baud to see status messages.

## Usage

- **Record:** press Record to start and press it again to stop. Files are saved as `REC001.mp3`, `REC002.mp3`, and so on.
- **Play:** press Play to play the selected file. Press it again to stop.
- **Next:** press Next to choose the next MP3 on the card. It wraps around, and pressing it during playback skips to the next file.

## Troubleshooting

- **"SD card init failed":** check the wiring, the FAT32 format, and the CS pin.
- **Silent or noisy recordings:** check the mic's SEL pin, the 3.3 V supply, and that the I2S pins match the code.
- **No playback sound:** check the amp's wiring and speaker polarity. Power the amp from 5 V for more volume.
- **Choppy audio:** use a faster SD card, or lower `SAMPLE_RATE` in the sketch (for example, 22050).
- **Compile errors about codec headers:** library versions differ. Adjust the `#include "AudioTools/AudioCodecs/..."` lines to match the installed version.
