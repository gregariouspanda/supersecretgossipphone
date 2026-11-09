/*
  Super Secret Gossip Phone
  ESP32 MP3 recorder / player  (Adafruit Feather ESP32-S3 + microSD breakout)

  Records from an I2S microphone to MP3 files on a microSD card and plays
  them back through an I2S amplifier. It is controlled with an old rotary
  phone dial, a RECORD/LISTEN toggle switch and a 20x4 LCD.

  LIBRARIES (Arduino Library Manager or GitHub):
    - arduino-audio-tools  (pschatzmann)
    - arduino-libshine     (pschatzmann)  MP3 *encoder*
    - arduino-libhelix     (pschatzmann)  MP3 *decoder*
    - LiquidCrystal I2C    (Frank de Brabander)
  BOARD: "Adafruit Feather ESP32-S3 ..." (ESP32 board package by Espressif)

  HARDWARE (suggested; change pins below to suit)
    microSD breakout : SCK/MOSI/MISO -> Feather SCK/MOSI/MISO, CS -> D10
    I2S mic (e.g. SPH0645, ICS-43434, INMP441): BCLK->D9, WS->D6, DOUT->D12
    I2S amp (e.g. MAX98357A)                  : BCLK->D9, LRC->D6, DIN->D5
      (mic and amp share BCLK/WS; only the data pins differ)
    Rotary phone dial, pulse contact : one wire to A3, the other to GND
    Mode toggle (on/on SPDT)         : common->D13, one throw to GND (LISTEN),
                                       other throw to 3V3 (RECORD)
    Recording LED (external)         : D11 -> 330 ohm -> LED anode, cathode -> GND
    LCD (20x4 I2C backpack, 0x27)    : SDA/SCL, power from USB 5V

  USE   (flip the toggle to RECORD or LISTEN, then dial)
    RECORD mode
      dial 9 : record. Dial 9 again to stop. Recording also stops by itself
               after 5 minutes. Files are REC001.mp3, REC002.mp3, ...
      dial 8 : keep. Locks in the take you just made.
      dial 5 : delete and re-record. Within 1 minute after a recording ends,
               deletes that take and records over it. Only the most recent
               recording can be replaced.
    LISTEN mode
      dial the recording number (for example 5 plays REC005, 1 2 plays REC012;
      up to 3 digits). It plays about 2 seconds after the last digit.
      Dial any digit while it is playing to stop.
    LED : on while recording; off for 2 s at the 1 minute mark;
          blinks during the last 30 s; off when not recording.
    Serial Monitor at 115200 shows status.

  NOTE: Pin numbers and library header paths can differ between board/library
  versions. Verify against your Feather's pinout and the installed
  arduino-audio-tools version.
*/

#include <SPI.h>
#include <SD.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Shine.h"   // encoder
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"   // decoder
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------------- Configuration ----------------
const int SD_CS        = 10;

const int PIN_I2S_BCLK = 9;
const int PIN_I2S_WS   = 6;
const int PIN_I2S_OUT  = 5;    // to amp DIN
const int PIN_I2S_IN   = 12;   // from mic DOUT

const int PIN_DIAL     = A3;   // rotary dial pulse contact (other side to GND)
const int PIN_MODE     = 13;   // on/on toggle: HIGH = RECORD, LOW = LISTEN
const int PIN_LED      = 11;   // external recording LED (via 330 ohm resistor)

const uint8_t LCD_ADDR = 0x27; // try 0x3F if the screen stays blank
const int LCD_COLS     = 20;
const int LCD_ROWS     = 4;

const int SAMPLE_RATE  = 44100;  // Shine supports 32000/44100/48000 (and lower MPEG-2 rates)
const int CHANNELS     = 1;      // mono mic
const int BITS         = 16;

// Dial commands (RECORD mode)
const int DIGIT_RECORD = 9;
const int DIGIT_KEEP   = 8;
const int DIGIT_REDO   = 5;

// Dial timing. The pulse contact is closed to GND at rest and opens once per pulse.
const unsigned long PULSE_DEBOUNCE_MS = 12;
const unsigned long PULSE_END_MS      = 250;   // no pulse this long = digit finished
const unsigned long NUMBER_ENTRY_MS   = 2000;  // LISTEN: play this long after the last digit
const int MAX_NUMBER_DIGITS           = 3;

// Recording limit and LED pattern (all times in milliseconds since recording began)
const unsigned long MAX_RECORD_MS   = 5UL * 60UL * 1000UL;  // stop automatically at 5 minutes
const unsigned long LED_GAP_START   = 60UL * 1000UL;         // LED goes dark at 1:00 ...
const unsigned long LED_GAP_MS      = 2000UL;                // ... for 2 seconds
const unsigned long LED_BLINK_START = MAX_RECORD_MS - 30UL * 1000UL;  // blink for the last 30 s
const unsigned long LED_BLINK_HALF  = 500UL;                 // 0.5 s on / 0.5 s off
const unsigned long REDO_WINDOW_MS  = 60UL * 1000UL;         // delete and re-record allowed for 1 minute
// -----------------------------------------------

enum State { IDLE, RECORDING, PLAYING };
State state = IDLE;

AudioInfo audioInfo(SAMPLE_RATE, CHANNELS, BITS);

LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

I2SStream i2s;
File file;

// Recording chain: mic (I2S) -> MP3 encoder -> SD file
MP3EncoderShine mp3Encoder;
EncodedAudioStream encoderStream(&file, &mp3Encoder);
StreamCopy recordCopier(encoderStream, i2s);

// Playback chain: SD file -> MP3 decoder -> amp (I2S)
MP3DecoderHelix mp3Decoder;
EncodedAudioStream decoderStream(&i2s, &mp3Decoder);
StreamCopy playCopier(decoderStream, file);

String currentFile = "";           // file being played / last file chosen
unsigned long recordStartMs = 0;
String lastRecording = "";         // most recent take that can still be kept or replaced
unsigned long lastRecordEndMs = 0;
String notice = "";                // short message shown on the LCD
unsigned long noticeUntilMs = 0;

String numberBuf = "";             // LISTEN: digits dialed so far
unsigned long lastDigitMs = 0;

// ---------------- Rotary dial ----------------
// Returns the dialed digit (0-9) once the dial has finished returning, or -1.
int readDial() {
  static bool lastLevel = LOW;
  static unsigned long lastEdge = 0;
  static int pulses = 0;

  bool level = digitalRead(PIN_DIAL);
  if (level != lastLevel && millis() - lastEdge > PULSE_DEBOUNCE_MS) {
    lastLevel = level;
    lastEdge = millis();
    if (level == HIGH) pulses++;          // contact opened: one pulse
  }
  if (pulses > 0 && lastLevel == LOW && millis() - lastEdge > PULSE_END_MS) {
    int digit = (pulses >= 10) ? 0 : pulses;   // ten pulses is the digit 0
    pulses = 0;
    return digit;
  }
  return -1;
}

// ---------------- Recording LED ----------------
bool ledShouldBeOn(unsigned long elapsed) {
  if (elapsed >= LED_BLINK_START) return ((elapsed - LED_BLINK_START) / LED_BLINK_HALF) % 2 == 0;
  if (elapsed >= LED_GAP_START && elapsed < LED_GAP_START + LED_GAP_MS) return false;
  return true;
}

// ---------------- File helpers ----------------
bool isMp3(const String &name) {
  String n = name;
  n.toLowerCase();
  return n.endsWith(".mp3") && !n.startsWith(".");
}

String stripSlash(String n) { return n.startsWith("/") ? n.substring(1) : n; }

String recPath(int n) {
  char buf[20];
  snprintf(buf, sizeof(buf), "/REC%03d.mp3", n);
  return String(buf);
}

// Next unused name: /REC001.mp3, /REC002.mp3, ...
String nextRecordingName() {
  for (int i = 1; i < 1000; i++) {
    String p = recPath(i);
    if (!SD.exists(p)) return p;
  }
  return recPath(999);
}

int countMp3() {
  int total = 0;
  File root = SD.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String name = stripSlash(String(f.name()));
    bool dir = f.isDirectory();
    f.close();
    if (!dir && isMp3(name)) total++;
  }
  root.close();
  return total;
}

// ---------------- LCD ----------------
String lcdRows[LCD_ROWS];
void lcdLine(int row, String text) {
  while ((int)text.length() < LCD_COLS) text += ' ';
  text = text.substring(0, LCD_COLS);
  if (text != lcdRows[row]) {      // only touch the screen when a row changes
    lcdRows[row] = text;
    lcd.setCursor(0, row);
    lcd.print(text);
  }
}
String shortName(const String &path) { return stripSlash(path); }
String mmss(unsigned long ms) {
  unsigned long s = ms / 1000;
  char b[8]; snprintf(b, sizeof(b), "%02lu:%02lu", s / 60, s % 60);
  return String(b);
}
bool recordMode() { return digitalRead(PIN_MODE) == HIGH; }
void showNotice(const String &msg, unsigned long ms = 2500) { notice = msg; noticeUntilMs = millis() + ms; }

void updateDisplay() {
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw < 200) return;
  lastDraw = millis();

  if (state == RECORDING) {
    lcdLine(0, "RECORDING");
    lcdLine(1, shortName(currentFile));
    lcdLine(2, mmss(millis() - recordStartMs) + " / " + mmss(MAX_RECORD_MS));
    lcdLine(3, "Dial 9 to stop");
  } else if (state == PLAYING) {
    lcdLine(0, "PLAYING");
    lcdLine(1, shortName(currentFile));
    lcdLine(2, "");
    lcdLine(3, "Dial any digit: stop");
  } else if (recordMode()) {
    lcdLine(0, "RECORD MODE");
    unsigned long since = millis() - lastRecordEndMs;
    bool canRedo = lastRecording != "" && since < REDO_WINDOW_MS;
    if (millis() < noticeUntilMs) {
      lcdLine(1, notice);
      lcdLine(2, "");
    } else if (canRedo) {
      lcdLine(1, "Saved " + shortName(lastRecording));
      lcdLine(2, "Keep/redo: " + String((REDO_WINDOW_MS - since) / 1000 + 1) + "s left");
    } else {
      lcdLine(1, "Next: " + shortName(nextRecordingName()));
      lcdLine(2, "");
    }
    lcdLine(3, canRedo ? "8 keep 5 redo 9 new" : "Dial 9 to record");
  } else {
    lcdLine(0, "LISTEN  " + String(countMp3()) + " files");
    if (millis() < noticeUntilMs) {
      lcdLine(1, notice); lcdLine(2, ""); lcdLine(3, "");
    } else if (numberBuf != "") {
      int n = numberBuf.toInt();
      String p = recPath(n);
      lcdLine(1, "Dial: " + numberBuf);
      lcdLine(2, SD.exists(p) ? shortName(p) : shortName(p) + " ?");
      lcdLine(3, SD.exists(p) ? "Playing soon..." : "Not on the card");
    } else {
      lcdLine(1, "Dial the number of");
      lcdLine(2, "the recording to");
      lcdLine(3, "play, e.g. 5 = REC005");
    }
  }
}

// ---------------- Actions ----------------
void startRecording(bool redo) {
  String path;
  if (redo) {
    path = lastRecording;
    SD.remove(path);               // delete the old take first (FILE_WRITE would append)
    Serial.print("Deleted "); Serial.println(path);
  } else {
    path = nextRecordingName();
  }
  file = SD.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("ERROR: could not open file for writing");
    showNotice("SD write error");
    lastRecording = "";
    return;
  }
  encoderStream.begin(audioInfo);   // starts MP3 encoder
  currentFile = path;
  recordStartMs = millis();
  digitalWrite(PIN_LED, HIGH);
  state = RECORDING;
  Serial.print("Recording -> "); Serial.println(path);
}

void stopRecording() {
  encoderStream.end();   // flushes last MP3 frame
  file.close();
  digitalWrite(PIN_LED, LOW);
  lastRecording = currentFile;
  lastRecordEndMs = millis();
  state = IDLE;
  Serial.print("Saved "); Serial.println(currentFile);
}

void startPlayback(const String &path) {
  file = SD.open(path, FILE_READ);
  if (!file) {
    Serial.println("ERROR: could not open file for reading");
    showNotice("Cannot open file");
    return;
  }
  currentFile = path;
  decoderStream.begin();   // decoder reconfigures I2S from the MP3 header
  state = PLAYING;
  Serial.print("Playing "); Serial.println(path);
}

void stopPlayback() {
  decoderStream.end();
  file.close();
  // restore I2S settings used for recording
  i2s.setAudioInfo(audioInfo);
  state = IDLE;
  Serial.println("Playback stopped");
}

bool redoAllowed() {
  return lastRecording != "" && (millis() - lastRecordEndMs) < REDO_WINDOW_MS && SD.exists(lastRecording);
}

void handleDigit(int d) {
  Serial.print("Dial: "); Serial.println(d);

  if (state == PLAYING) { stopPlayback(); return; }   // any digit stops playback

  if (recordMode()) {
    if (state == RECORDING) {
      if (d == DIGIT_RECORD) stopRecording();
      return;
    }
    if (d == DIGIT_RECORD) {
      startRecording(false);
    } else if (d == DIGIT_KEEP) {
      if (redoAllowed()) { showNotice("Kept " + shortName(lastRecording)); lastRecording = ""; }
      else showNotice("Nothing to keep");
    } else if (d == DIGIT_REDO) {
      if (redoAllowed()) startRecording(true);
      else showNotice("Too late to redo");
    }
  } else if (state == IDLE) {
    if ((int)numberBuf.length() < MAX_NUMBER_DIGITS) numberBuf += String(d);
    lastDigitMs = millis();
  }
}

void playDialedNumber() {
  int n = numberBuf.toInt();
  numberBuf = "";
  String p = recPath(n);
  if (n > 0 && SD.exists(p)) startPlayback(p);
  else showNotice(shortName(p) + " not found");
}

// ---------------- Arduino ----------------
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_DIAL, INPUT_PULLUP);
  pinMode(PIN_MODE, INPUT);   // driven by the toggle (GND or 3V3), never floating
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  Wire.begin();
  Wire.setClock(400000);
  lcd.init();
  lcd.backlight();
  lcdLine(0, "Gossip Phone");
  lcdLine(1, "Starting...");

  if (!SD.begin(SD_CS)) {
    Serial.println("SD card init failed - check wiring/format (FAT32)");
    lcdLine(1, "SD card failed");
    lcdLine(2, "Check wiring/FAT32");
    while (true) delay(1000);
  }
  Serial.println("SD card ready");
  lcdLine(1, "SD card ready");

  // Full-duplex I2S: mic on RX data pin, amp on TX data pin, shared clocks.
  auto cfg = i2s.defaultConfig(RXTX_MODE);
  cfg.copyFrom(audioInfo);
  cfg.pin_bck     = PIN_I2S_BCLK;
  cfg.pin_ws      = PIN_I2S_WS;
  cfg.pin_data    = PIN_I2S_OUT;
  cfg.pin_data_rx = PIN_I2S_IN;
  i2s.begin(cfg);

  Serial.println("Ready.  Toggle = RECORD/LISTEN.  RECORD: dial 9=record 8=keep 5=redo.  LISTEN: dial a number");
}

void loop() {
  // Mode toggle: flipping it stops whatever is running.
  bool recMode = recordMode();
  static bool lastRecMode = recMode;
  if (recMode != lastRecMode) {
    lastRecMode = recMode;
    if (state == RECORDING) stopRecording();
    else if (state == PLAYING) stopPlayback();
    numberBuf = "";
  }

  int digit = readDial();
  if (digit >= 0) handleDigit(digit);

  // LISTEN: play the dialed number once the dialing pause is over.
  if (state == IDLE && !recMode && numberBuf != "" &&
      (millis() - lastDigitMs > NUMBER_ENTRY_MS || (int)numberBuf.length() >= MAX_NUMBER_DIGITS)) {
    if (millis() - lastDigitMs > 800) playDialedNumber();
  }

  switch (state) {
    case IDLE:
      break;

    case RECORDING: {
      recordCopier.copy();            // mic -> MP3 -> SD
      unsigned long elapsed = millis() - recordStartMs;
      digitalWrite(PIN_LED, ledShouldBeOn(elapsed) ? HIGH : LOW);
      if (elapsed >= MAX_RECORD_MS) {
        Serial.println("5 minute limit reached");
        stopRecording();
      }
      break;
    }

    case PLAYING:
      if (playCopier.copy() == 0 && !file.available()) {
        stopPlayback();               // reached end of file
      }
      break;
  }

  updateDisplay();
}
