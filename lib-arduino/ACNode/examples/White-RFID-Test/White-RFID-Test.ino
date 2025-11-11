
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <ExpandedGPIO.h>

#ifndef ARDUINO_ESP32_WROOM_DA
#error "White/Black/Blue Hardware is expected to be an ESP32 WROOM-DA"
#endif

// Convenience shorthands
int xdigitalRead(uint8_t pin) {
  return ExpandedGPIO::getInstance().xdigitalRead(pin);
};
void xdigitalWrite(uint8_t pin, uint8_t val) {
  ExpandedGPIO::getInstance().xdigitalWrite(pin, val);
};
void xanalogWrite(uint8_t pin, uint8_t val) {
  ExpandedGPIO::getInstance().xanalogWrite(pin, val);
};
unsigned int xanalogRead(uint8_t pin) {
  return ExpandedGPIO::getInstance().xanalogRead(pin);
};
void xpinMode(uint8_t pin, uint8_t mode) {
  ExpandedGPIO::getInstance().xpinMode(pin, mode);
};


// Settings for White and newer
constexpr uint8_t RFID_RST_PIN = 32;  // Configurable, see typical pin layout above
constexpr uint8_t RFID_SCL = 15;
constexpr uint8_t RFID_SDA = 5;
constexpr byte RFID_ADDR = 0x28;

TwoWire i2cBus = TwoWire(0);
MFRC522_I2C dev = MFRC522_I2C(RFID_RST_PIN, RFID_ADDR, i2cBus);
MFRC522 mfrc522 = MFRC522(&dev);

#define ENABLE_SCREEN
#ifdef ENABLE_SCREEN
const uint8_t SCREEN_Address = 0x3c;
const uint8_t SCREEN_WIDTH = 128;  // OLED display width, in pixels
const uint8_t SCREEN_HEIGHT = 64;  // OLED display height, in pixels
const uint8_t SCREEN_RESET = -1;   //  Not wired up

Adafruit_SH1106G* display;
#endif

void setup() {
  Serial.begin(115200);  // Initialize serial communications with the PC
  delay(1000);           // give the terminal a second to connect, etc after a
  Serial.println("\n\n\nStarting RFID init....\n\n");

  i2cBus.begin(RFID_SDA, RFID_SCL, 200000);
  Serial.println("begin()");
  ExpandedGPIO::getInstance().addAW9523(0x58, &i2cBus);
  Serial.println("expander()");

  const byte LEDA = PIN_HPIO_AW9523 | (8 + 0);  // P1_0 -- checked on blue board
  xpinMode(LEDA, AW9523_LED_MODE);
  xanalogWrite(LEDA, 100);
  Serial.println("LED half on");


  // Reduce the current to a sensible level.
  // Awaiting https://github.com/adafruit/Adafruit_AW9523/pull/5.
  //
  i2cBus.beginTransmission(0x58);
  i2cBus.write(0x11);
  i2cBus.write(3);
  i2cBus.endTransmission();
  Serial.println("power down()");

  mfrc522.PCD_Init();                 // Init MFRC522
  mfrc522.PCD_DumpVersionToSerial();  // Show details of PCD - MFRC522 Card Reader details

  Serial.println("rfid init()");

#ifdef ENABLE_SCREEN
  display = new Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &i2cBus, SCREEN_RESET, 400000, 100000);
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
  } else
#endif
  {
    Serial.println("No screen");
  }

  Serial.println(F("Starting scan PICC to see UID, SAK, type, and data blocks..."));
}

void loop() {
#ifdef ENABLE_SCREEN
  static unsigned long lst = 0;
  if ((millis() > lst + 1000) && (display)) {
    lst = millis();
    display->clearDisplay();
    display->setCursor(0, 0);
    static unsigned long i = 0;
    display->println(++i);
    display->display();
  }
#endif

  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;  // no card in sight.
  }

  // Select one of the cards
  if (!mfrc522.PICC_ReadCardSerial()) {
    Serial.println("Bad read (was card removed too quickly?)");
    return;
  };

  if (mfrc522.uid.size == 0) {
    Serial.println("Bad card read (size = 0)");
    return;
  }

  char buff[sizeof(mfrc522.uid.uidByte) * 5 + 1] = { 0 };
  for (int i = 0; i < mfrc522.uid.size; i++) {
    char tag[5];  // 3 digits, dash and \0.
    snprintf(tag, sizeof(tag), "%s%d", i ? "-" : "", mfrc522.uid.uidByte[i]);
    strncat(buff, tag, sizeof(tag));
  };
  Serial.printf("Good scan (len=%d): ", mfrc522.uid.size);
  Serial.println(buff);
#ifdef ENABLE_SCREEN
  if (display) {
    display->println(buff);
    display->display();
  };
#endif  // disengage with the card.
  //
  mfrc522.PICC_HaltA();
}
