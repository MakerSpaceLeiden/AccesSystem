
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#ifndef ARDUINO_ESP32_WROOM_DA
#error "White/Black/Blue Hardware is expected to be an ESP32 WROOM-DA"
#endif

// Settings for White and newer
constexpr uint8_t I2C_RST_PIN = 32;  // Configurable, see typical pin layout above
constexpr uint8_t I2C_SCL = 15;
constexpr uint8_t I2C_SDA = 5;

const uint8_t SCREEN_Address = 0x3c;
const uint8_t SCREEN_WIDTH = 128;  // OLED display width, in pixels
const uint8_t SCREEN_HEIGHT = 64;  // OLED display height, in pixels
const uint8_t SCREEN_RESET = -1;   //  Not wired up

Adafruit_SH1106G* display;

void setup() {
  Serial.begin(115200);  // Initialize serial communications with the PC
  delay(1000);           // give the terminal a second to connect, etc after a
  Serial.println("\n\n\nStarting RFID init....\n\n");

  Wire.begin(I2C_SDA, I2C_SCL, 200000);

  display = new Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, SCREEN_RESET, 400000, 100000);
  
  if (display) {
    Serial.println("We hava a screen");
    display->setRotation(2);
    display->begin(SCREEN_Address, false);
    display->setCursor(0, 0);
    // display->setFont(FONT_SMALL);
    display->setTextSize(1);
    display->setTextColor(SH110X_WHITE);
    display->oled_command(SH110X_DISPLAYON);
    display->display();
  } else {
    Serial.printf("No screen found at 0x%02x\n", SCREEN_Address);
  }
}

void loop() {
  static unsigned long lst = 0;
  if ((millis() > lst + 1000) && (display)) {
    lst = millis();
    display->clearDisplay();
    display->setCursor(0, 0);
    static unsigned long i = 0;
    display->println(++i);
    display->display();
  }
}
