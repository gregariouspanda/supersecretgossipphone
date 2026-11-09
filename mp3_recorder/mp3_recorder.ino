/*
  ESP32 MP3 Recorder / Player  (Adafruit Feather ESP32-S3 + microSD breakout)

  Records from an I2S microphone to MP3 files on a microSD card, and plays
  them back through an I2S amplifier.

  LIBRARIES (Arduino Library Manager or GitHub -> install all three):
    - arduino-audio-tools  (pschatzmann)
    - arduino-libshine     (pschatzmann)  MP3 *encoder*
    - arduino-libhelix     (pschatzmann)  MP3 *decoder*
  BOARD: "Adafruit Feather ESP32-S3 ..." (ESP32 board package by Espressif)

  HARDWARE (suggested; change pins below to suit)
    microSD breakout : SCK/MOSI/MISO -> Feather SCK/MOSI/MISO, CS -> D10
    I2S mic (e.g. SPH0645, ICS-43434, INMP441): BCLK->D9, WS->D6, DOUT->D12
    I2S amp (e.g. MAX98357A)                  : BCLK->D9, LRC->D6, DIN->D5
      (mic and amp share BCLK/WS; only the data pins differ)
    Buttons (to GND, internal pull-ups): RECORD->A0, PLAY/STOP->A1, NEXT->A2
    Recording LED (external): D11 -> 330 ohm -> LED anode, LED cathode -> GND

    LCD (20x4 I2C, PCF8574 backpack, address 0x27): SDA/SCL, power from USB 5V
    Rotary encoder with push switch: A->A3, B->A4, click->A5 (common to GND)
    Mode toggle (on/on SPDT): common->D13, one throw to GND (LISTEN), other to 3V3 (RECORD)

  LIBRARY (extra): "LiquidCrystal I2C" by Frank de Brabander

  USE
    MODE toggle   : RECORD or LISTEN
    LISTEN mode   : turn the dial to scroll through the MP3 files (shown on the LCD),
                    click to play the selected file, click again to stop.
    RECORD mode   : click the dial to start recording, click again to stop.
                    Within 1 minute after a recording ends, a click starts a re-record.
    The three push buttons below still work in either mode.
    RECORD button : start / stop recording (new file REC001.mp3, REC002.mp3...)
                    Recording stops by itself after 5 minutes.
                    Re-record: pressing RECORD within 1 minute after a recording ends
                    deletes that recording and records over it. Only the most recent
                    recording can be replaced, and only inside that 1 minute window.
    LED           : on while recording; off for 2 s at the 1 minute mark;
                    blinks during the last 30 s; off when not recording.
    PLAY button   : play the selected file / stop playback
    NEXT button   : select next file on the card (wraps around)
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

const int BTN_RECORD   = A0;
const int BTN_PLAY     = A1;
const int BTN_NEXT     = A2;

const int PIN_ENC_A    = A3;   // rotary encoder
const int PIN_ENC_B    = A4;
const int PIN_ENC_SW   = A5;   // encoder push switch
const int PIN_MODE     = 13;   // on/on toggle: HIGH = RECORD, LOW = LISTEN

const uint8_t LCD_ADDR = 0x27; // try 0x3F if the screen stays blank
const int LCD_COLS     = 20;
const int LCD_ROWS     = 4;

const int PIN_LED      = 11;   // external recording LED (via 330 ohm resistor)

const int SAMPLE_RATE  = 44100;  // Shine supports 32000/44100/48000 (and lower MPEG-2 rates)
const int CHANNELS     = 1;      // mono mic
const int BITS         = 16;

const unsigned long DEBOUNCE_MS = 40;

// Recording limit and LED pattern (all times in milliseconds since recording began)
const unsigned long MAX_RECORD_MS   = 5UL * 60UL * 1000UL;  // stop automatically at 5 minutes
const unsigned long LED_GAP_START   = 60UL * 1000UL;         // LED goes dark at 1:00 ...
const unsigned long LED_GAP_MS      = 2000UL;                // ... for 2 seconds
const unsigned long LED_BLINK_START = MAX_RECORD_MS - 30UL * 1000UL;  // blink for the last 30 s
const unsigned long LED_BLINK_HALF  = 500UL;                 // 0.5 s on / 0.5 s off
const unsigned long REDO_WINDOW_MS  = 60UL * 1000UL;         // re-record allowed for 1 minute after a recording ends
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

String currentFile = "";   // file selected for playback
unsigned long recordStartMs = 0;
String lastRecording = "";         // most recent recording made since power-up
unsigned long lastRecordEndMs = 0;

// ---------------- Button helper ----------------
struct Button {
  int pin;
  bool lastReading = HIGH;
  bool stable = HIGH;
  unsigned long lastChange = 0;

  explicit Button(int p) : pin(p) {}
  void begin() { pinMode(pin, INPUT_PULLUP); }
  // returns true once per press
  bool pressed() {
    bool r = digitalRead(pin);
    if (r != lastReading) { lastReading = r; lastChange = millis(); }
    if (millis() - lastChange > DEBOUNCE_MS && r != stable) {
      stable = r;
      if (stable == LOW) return true;
    }
    return false;
  }
};

Button btnRecord(BTN_RECORD), btnPlay(BTN_PLAY), btnNext(BTN_NEXT), btnEnc(PIN_ENC_SW);

// ---------------- Rotary encoder (interrupt driven) ----------------
volatile int32_t encCount = 0;
volatile uint8_t encState = 0;
void IRAM_ATTR encISR() {
  static const int8_t table[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
  encState = ((encState << 2) | (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B)) & 0x0F;
  encCount += table[encState];
}
// Returns detent steps since last call (+1 = clockwise). One detent = 4 quadrature counts.
int encoderSteps() {
  static int32_t consumed = 0;
  noInterrupts(); int32_t c = encCount; interrupts();
  int steps = (c - consumed) / 4;
  consumed += steps * 4;
  return steps;
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

// Next unused name: /REC001.mp3, /REC002.mp3, ...
String nextRecordingName() {
  char buf[20];
  for (int i = 1; i < 1000; i++) {
    snprintf(buf, sizeof(buf), "/REC%03d.mp3", i);
    if (!SD.exists(buf)) return String(buf);
  }
  return String("/REC999.mp3");
}

// Returns the mp3 that follows `after` alphabetically (wraps). "" if none.
String nextFileAfter(const String &after) {
  String first = "", next = "";
  File root = SD.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String name = stripSlash(String(f.name()));
    bool dir = f.isDirectory();
    f.close();
    if (dir || !isMp3(name)) continue;
    name = "/" + name;
    if (first == "" || name < first) first = name;
    if (name > after && (next == "" || name < next)) next = name;
  }
  root.close();
  return next != "" ? next : first;
}

// Returns the mp3 that precedes `before` alphabetically (wraps). "" if none.
String prevFileBefore(const String &before) {
  String last = "", prev = "";
  File root = SD.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String name = stripSlash(String(f.name()));
    bool dir = f.isDirectory();
    f.close();
    if (dir || !isMp3(name)) continue;
    name = "/" + name;
    if (last == "" || name > last) last = name;
    if (name < before && (prev == "" || name > prev)) prev = name;
  }
  root.close();
  return prev != "" ? prev : last;
}

// Position (1-based) of `cur` among the MP3 files, and how many there are.
void fileIndex(const String &cur, int &idx, int &total) {
  idx = 0; total = 0;
  File root = SD.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String name = stripSlash(String(f.name()));
    bool dir = f.isDirectory();
    f.close();
    if (dir || !isMp3(name)) continue;
    total++;
    if (("/" + name) <= cur) idx++;
  }
  root.close();
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

void updateDisplay() {
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw < 200) return;
  lastDraw = millis();

  if (state == RECORDING) {
    lcdLine(0, "RECORDING");
    lcdLine(1, shortName(currentFile));
    lcdLine(2, mmss(millis() - recordStartMs) + " / " + mmss(MAX_RECORD_MS));
    lcdLine(3, "Click dial to stop");
  } else if (state == PLAYING) {
    int idx, total; fileIndex(currentFile, idx, total);
    lcdLine(0, "PLAYING  " + String(idx) + "/" + String(total));
    lcdLine(1, shortName(currentFile));
    lcdLine(2, "");
    lcdLine(3, "Click dial to stop");
  } else if (recordMode()) {
    lcdLine(0, "RECORD MODE");
    unsigned long since = millis() - lastRecordEndMs;
    if (lastRecording != "" && since < REDO_WINDOW_MS) {
      lcdLine(1, "Saved " + shortName(lastRecording));
      lcdLine(2, "Re-record: " + String((REDO_WINDOW_MS - since) / 1000 + 1) + "s left");
      lcdLine(3, "Click = redo take");
    } else {
      lcdLine(1, "Next: " + shortName(nextRecordingName()));
      lcdLine(2, "Max 5:00");
      lcdLine(3, "Click dial to start");
    }
  } else {
    int idx = 0, total = 0;
    if (currentFile != "") fileIndex(currentFile, idx, total);
    lcdLine(0, "LISTEN  " + String(idx) + "/" + String(total));
    if (currentFile == "") {
      lcdLine(1, "No MP3 files"); lcdLine(2, ""); lcdLine(3, "");
    } else {
      lcdLine(1, " " + shortName(prevFileBefore(currentFile)));
      lcdLine(2, ">" + shortName(currentFile));
      lcdLine(3, " " + shortName(nextFileAfter(currentFile)));
    }
  }
}

// ---------------- Actions ----------------
void startRecording() {
  // Within 1 minute of the last recording ending, RECORD replaces that recording.
  bool redo = lastRecording != "" && (millis() - lastRecordEndMs) < REDO_WINDOW_MS && SD.exists(lastRecording);
  String path = redo ? lastRecording : nextRecordingName();
  if (redo) {
    SD.remove(path);               // delete the old take first (FILE_WRITE would append)
    Serial.print("Deleted "); Serial.println(path);
  }
  file = SD.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("ERROR: could not open file for writing");
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

void startPlayback() {
  if (currentFile == "") currentFile = nextFileAfter("");
  if (currentFile == "") {
    Serial.println("No MP3 files on card");
    return;
  }
  file = SD.open(currentFile, FILE_READ);
  if (!file) {
    Serial.println("ERROR: could not open file for reading");
    return;
  }
  decoderStream.begin();   // decoder reconfigures I2S from the MP3 header
  state = PLAYING;
  Serial.print("Playing "); Serial.println(currentFile);
}

void stopPlayback() {
  decoderStream.end();
  file.close();
  // restore I2S settings used for recording
  i2s.setAudioInfo(audioInfo);
  state = IDLE;
  Serial.println("Playback stopped");
}

void selectNext() {
  String n = nextFileAfter(currentFile);
  if (n == "") { Serial.println("No MP3 files on card"); return; }
  currentFile = n;
  Serial.print("Selected "); Serial.println(currentFile);
}

void selectPrev() {
  String n = prevFileBefore(currentFile);
  if (n == "") { Serial.println("No MP3 files on card"); return; }
  currentFile = n;
  Serial.print("Selected "); Serial.println(currentFile);
}

// ---------------- Arduino ----------------
void setup() {
  Serial.begin(115200);
  delay(500);

  btnRecord.begin(); btnPlay.begin(); btnNext.begin(); btnEnc.begin();
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_MODE, INPUT);   // driven by the toggle (GND or 3V3), never floating
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);
  Wire.begin();
  Wire.setClock(400000);
  lcd.init();
  lcd.backlight();
  lcdLine(0, "Gossip Phone");
  lcdLine(1, "Starting...");
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

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

  currentFile = nextFileAfter("");   // select first existing file, if any
  Serial.println("Ready.  A0=record (max 5 min)  A1=play/stop  A2=next file");
}

void loop() {
  bool rec = btnRecord.pressed();
  bool ply = btnPlay.pressed();
  bool nxt = btnNext.pressed();

  // Rotary dial: click acts as Record or Play depending on the mode toggle.
  bool click = btnEnc.pressed();
  bool recMode = recordMode();
  static bool lastRecMode = recMode;
  if (recMode != lastRecMode) {            // toggle flipped: stop whatever is running
    lastRecMode = recMode;
    if (state == RECORDING) stopRecording();
    else if (state == PLAYING) stopPlayback();
    click = false;
  }
  if (click) { if (recMode) rec = true; else ply = true; }
  int turn = encoderSteps();               // scrolls the file list in LISTEN mode
  if (state == IDLE && !recMode) {
    while (turn > 0) { selectNext(); turn--; }
    while (turn < 0) { selectPrev(); turn++; }
  }

  switch (state) {
    case IDLE:
      if (rec) startRecording();
      else if (ply) startPlayback();
      else if (nxt) selectNext();
      break;

    case RECORDING:
      recordCopier.copy();            // mic -> MP3 -> SD
      {
        unsigned long elapsed = millis() - recordStartMs;
        digitalWrite(PIN_LED, ledShouldBeOn(elapsed) ? HIGH : LOW);
        if (elapsed >= MAX_RECORD_MS) {
          Serial.println("5 minute limit reached");
          stopRecording();
        } else if (rec || ply) {
          stopRecording();
        }
      }
      break;

    case PLAYING:
      if (playCopier.copy() == 0 && !file.available()) {
        stopPlayback();               // reached end of file
      } else if (ply || rec) {
        stopPlayback();
      } else if (nxt) {
        stopPlayback();
        selectNext();
        startPlayback();
      }
      break;
  }

  updateDisplay();
}
