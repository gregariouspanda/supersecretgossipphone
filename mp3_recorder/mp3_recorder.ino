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

  USE
    RECORD button : start / stop recording (new file REC001.mp3, REC002.mp3...)
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

// ---------------- Configuration ----------------
const int SD_CS        = 10;

const int PIN_I2S_BCLK = 9;
const int PIN_I2S_WS   = 6;
const int PIN_I2S_OUT  = 5;    // to amp DIN
const int PIN_I2S_IN   = 12;   // from mic DOUT

const int BTN_RECORD   = A0;
const int BTN_PLAY     = A1;
const int BTN_NEXT     = A2;

const int SAMPLE_RATE  = 44100;  // Shine supports 32000/44100/48000 (and lower MPEG-2 rates)
const int CHANNELS     = 1;      // mono mic
const int BITS         = 16;

const unsigned long DEBOUNCE_MS = 40;
// -----------------------------------------------

enum State { IDLE, RECORDING, PLAYING };
State state = IDLE;

AudioInfo audioInfo(SAMPLE_RATE, CHANNELS, BITS);

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

Button btnRecord(BTN_RECORD), btnPlay(BTN_PLAY), btnNext(BTN_NEXT);

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

// ---------------- Actions ----------------
void startRecording() {
  String path = nextRecordingName();
  file = SD.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("ERROR: could not open file for writing");
    return;
  }
  encoderStream.begin(audioInfo);   // starts MP3 encoder
  currentFile = path;
  state = RECORDING;
  Serial.print("Recording -> "); Serial.println(path);
}

void stopRecording() {
  encoderStream.end();   // flushes last MP3 frame
  file.close();
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

// ---------------- Arduino ----------------
void setup() {
  Serial.begin(115200);
  delay(500);

  btnRecord.begin(); btnPlay.begin(); btnNext.begin();

  if (!SD.begin(SD_CS)) {
    Serial.println("SD card init failed - check wiring/format (FAT32)");
    while (true) delay(1000);
  }
  Serial.println("SD card ready");

  // Full-duplex I2S: mic on RX data pin, amp on TX data pin, shared clocks.
  auto cfg = i2s.defaultConfig(RXTX_MODE);
  cfg.copyFrom(audioInfo);
  cfg.pin_bck     = PIN_I2S_BCLK;
  cfg.pin_ws      = PIN_I2S_WS;
  cfg.pin_data    = PIN_I2S_OUT;
  cfg.pin_data_rx = PIN_I2S_IN;
  i2s.begin(cfg);

  currentFile = nextFileAfter("");   // select first existing file, if any
  Serial.println("Ready.  A0=record  A1=play/stop  A2=next file");
}

void loop() {
  bool rec = btnRecord.pressed();
  bool ply = btnPlay.pressed();
  bool nxt = btnNext.pressed();

  switch (state) {
    case IDLE:
      if (rec) startRecording();
      else if (ply) startPlayback();
      else if (nxt) selectNext();
      break;

    case RECORDING:
      recordCopier.copy();            // mic -> MP3 -> SD
      if (rec || ply) stopRecording();
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
}
