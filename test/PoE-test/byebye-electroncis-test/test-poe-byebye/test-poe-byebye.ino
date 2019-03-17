#include <Wire.h>
#include <MFRC522.h>

#include "OLED.h"

const uint8_t I2C_SDA_PIN = 13; //SDA;  // i2c SDA Pin, ext 2, pin 10
const uint8_t I2C_SCL_PIN = 16; //SCL;  // i2c SCL Pin, ext 2, pin 7

const uint8_t mfrc522_rfid_i2c_addr = 0x28;
const uint8_t mfrc522_rfid_i2c_irq = 4;   // Ext 1, pin 10
const uint8_t mfrc522_rfid_i2c_reset = 5; // Ext 1, pin  9

const uint8_t aart_led  = 15; // Ext 2, pin 8
const uint8_t pusbutton =  1; // Ext 1, pin 6

TwoWire i2cBus = TwoWire(0);
MFRC522_I2C * dev = new MFRC522_I2C(mfrc522_rfid_i2c_reset, mfrc522_rfid_i2c_addr, i2cBus);
MFRC522 mfrc522  = MFRC522(dev);

OLED oled = OLED();

void setup() {
  Serial.begin(115200);

  pinMode(aart_led, OUTPUT);
  digitalWrite(aart_led, LOW);
  pinMode(pusbutton, INPUT_PULLUP);


  i2cBus.begin(I2C_SDA_PIN, I2C_SCL_PIN); // , 50000);
  // Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN); //, 400000);

  Serial.print("Scanning I2C bus:");
  for (uint8_t address = 1; address < 127; address++ ) {
    i2cBus.beginTransmission(address);
    if (i2cBus.endTransmission() == 0) {
      Serial.print(" 0x"); Serial.print(address, HEX);
    };
  };
  Serial.println(".");

  oled.setup();
  oled.setText("started");

  mfrc522.PCD_Init();    // Init MFRC522
  mfrc522.PCD_DumpVersionToSerial();

}

void loop() {
  static unsigned long int last = 1;
  if (millis() - last > 5000 && last != 0) {
    oled.setText("Scan card...");
    last = 0;
  };
  oled.loop();

  // digitalWrite(aart_led, (millis() >> 10) & 1);
  digitalWrite(aart_led, digitalRead(pusbutton));

  if (mfrc522.PICC_IsNewCardPresent()) {
    Serial.println("Card seen");
    if (mfrc522.PICC_ReadCardSerial()) {
      Serial.println("Read started");
      if ( mfrc522.uid.size > 0) {
        Serial.println("Read OK");

        char buff[ 128 ] = { 0 };
        for (int i = 0; i < mfrc522.uid.size; i++) {
          char tag[5]; // 3 digits, dash and \0.
          snprintf(tag, sizeof(tag), "%s%d", i ? "-" : "", mfrc522.uid.uidByte[i]);
          strncat(buff, tag, sizeof(tag));
        };

        Serial.println("Good scan: ");
        Serial.println(buff);

        oled.setText(buff);
        last = millis();
      };
    };
    mfrc522.PICC_HaltA();
  };
}
