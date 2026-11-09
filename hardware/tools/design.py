"""Shared design data for the Super Secret Gossip Phone carrier board."""
# Feather ESP32-S3 header pin names, pin 1 first.
FEATHER_16 = ["RST","3V3","ARef","GND","A0","A1","A2","A3","A4","A5","SCK","MOSI","MISO","RX","TX","NC"]
FEATHER_12 = ["BAT","EN","USB","D13","D12","D11","D10","D9","D6","D5","SCL","SDA"]

# Net assigned to a Feather pin (others are unconnected).
FEATHER_NETS = {
    "3V3":"+3V3","GND":"GND","USB":"+5V",
    "A3":"DIAL","D13":"MODE","SCL":"I2C_SCL","SDA":"I2C_SDA",
    "SCK":"SD_SCK","MOSI":"SD_MOSI","MISO":"SD_MISO","D10":"SD_CS",
    "D11":"LED_DRV","D9":"I2S_BCLK","D6":"I2S_WS","D12":"I2S_MIC","D5":"I2S_SPK",
}
# Breakout headers, listed in the physical pin order of the Adafruit breakouts.
SD_PINS  = [("5V",None),("3V","+3V3"),("GND","GND"),("CLK","SD_SCK"),("DO","SD_MISO"),("DI","SD_MOSI"),("CS","SD_CS"),("CD",None)]
MIC_PINS = [("3V","+3V3"),("GND","GND"),("BCLK","I2S_BCLK"),("DOUT","I2S_MIC"),("LRCL","I2S_WS"),("SEL","GND")]
AMP_PINS = [("VIN","AMP_VIN"),("GND","GND"),("SD",None),("GAIN",None),("DIN","I2S_SPK"),("BCLK","I2S_BCLK"),("LRC","I2S_WS")]
JP_PINS  = [("5V","+5V"),("VIN","AMP_VIN"),("3V3","+3V3")]
LCD_PINS  = [("GND","GND"),("VCC","+5V"),("SDA","I2C_SDA"),("SCL","I2C_SCL")]
MODE_PINS = [("LISTEN","GND"),("COM","MODE"),("RECORD","+3V3")]
