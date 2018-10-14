#include <WiFi.h>
#include <ESPmDNS.h>
#include <Wire.h>

#define SSD1306_128_64 1

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <MFRC522_I2C.h>

#define LED_RED         (GPIO_NUM_22)
#define LED_GREEN       (GPIO_NUM_23)

#define LED_BLUE        (GPIO_NUM_2)

#define BUTTON_RED      (GPIO_NUM_19)
#define BUTTON_GREEN    (GPIO_NUM_21)

#define I2C_SCL         (GPIO_NUM_15)
#define I2C_SDA         (GPIO_NUM_4)

#define RFID_RST        (GPIO_NUM_5)
#define RFID_IRQ        (GPIO_NUM_18)

#include "/Users/dirkx/.passwd.h"


Adafruit_SSD1306 display(-1);  // -1 = no reset pin
MFRC522 mfrc522(0x28, RFID_RST); // Create MFRC522 instance.

const char name[] = "test-lcd-scr";

void i2c_scan() {
  byte error, address;
  int nDevices;

  Serial.print("I22 Scan: ");

  nDevices = 0;
  for (address = 1; address < 127; address++ )
  {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0)
    {
      if (nDevices)
        Serial.print(", ");

      Serial.print("0x");
      if (address < 16)
        Serial.print("0");
      Serial.print(address, HEX);

      nDevices++;
    }
    else if (error == 4)
    {
      Serial.print("Unknown error at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.println(address, HEX);
    }
  };

  if (nDevices == 0)
    Serial.println("none");
  else
    Serial.println(".");
}

void setup() {
  Serial.begin(115200);
  Serial.print("\n\n\nBooting ");
  Serial.println(__FILE__);
  Serial.println(__DATE__ " " __TIME__);

  delay(500);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NETWORK, WIFI_PASSWD);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(1000);
    ESP.restart();
  }

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  pinMode(BUTTON_RED, INPUT_PULLUP);
  pinMode(BUTTON_GREEN, INPUT_PULLUP);


  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  // the RFID reader needs to be in the right mode
  // *before* we scan/enable i2c.
  //
  pinMode(GPIO_NUM_5, OUTPUT);
  digitalWrite(GPIO_NUM_5, HIGH);

  Wire.begin(I2C_SDA, I2C_SCL, 0);
  i2c_scan();

  display.begin();
  display.invertDisplay(0);
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(WHITE);
  // display.setTextColor(BLACK, WHITE); // 'inverted' text
  display.setCursor(0, 0);
  display.print("IP: ");
  display.println(WiFi.localIP());

  mfrc522.PCD_Init();             // Init MFRC522
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  display.printf("MFRC522: 0x%02x %s", v, (v == 0x91) ? "v1.0" : (v == 0x92) ? "v2.0" : "unknown");
  display.display();

  rfid_setup();
  ota_setup();
  Serial.println("Started loop");
}

// Setup IRQ for RFID
volatile boolean newCard = false;

void rfid_irq_callback() {
  newCard = true;
}

void rfid_setup() {
  pinMode(RFID_IRQ, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RFID_IRQ), rfid_irq_callback, FALLING);
  mfrc522.PCD_WriteRegister(mfrc522.ComIEnReg, 0xA0 /* Read IRQ */ );
  newCard = false;
}

void rfid_reactivate(MFRC522 mfrc522) {
  mfrc522.PCD_WriteRegister(mfrc522.FIFODataReg, mfrc522.PICC_CMD_REQA);
  mfrc522.PCD_WriteRegister(mfrc522.CommandReg, mfrc522.PCD_Transceive);
  mfrc522.PCD_WriteRegister(mfrc522.BitFramingReg, 0x87);
}

void loop() {
  ota_loop();
  {
    static unsigned long t, s;
    if (millis() - t > 1000) {
      t = millis();
      digitalWrite(LED_BLUE, (s++) & 1);
      if (!newCard)
        rfid_reactivate(mfrc522);
    }
  }


  static unsigned long lastScan;
  if (millis() - lastScan > 2000) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.printf("Waiting for card");
    display.display();
    digitalWrite(LED_GREEN, !digitalRead(BUTTON_GREEN));
    digitalWrite(LED_RED, !digitalRead(BUTTON_RED));
  }

  if (newCard) {
    display.clearDisplay();
    display.setCursor(0, 0);

    if (mfrc522.PICC_ReadCardSerial()) {
      String uidStr = "";
      for (int i = 0; i < mfrc522.uid.size; i++) {
        if (i) uidStr += "-";
        uidStr += String(mfrc522.uid.uidByte[i]);
      };
      display.printf("Card %s (%d)", uidStr.c_str(), mfrc522.uid.size);
      digitalWrite(LED_GREEN, HIGH);
      digitalWrite(LED_RED, LOW);
    } else {
      digitalWrite(LED_GREEN, LOW);
      digitalWrite(LED_RED, HIGH);
      display.printf("Failed scan");
    }

    display.display();
    lastScan = millis();

    mfrc522.PICC_HaltA();
    mfrc522.PCD_WriteRegister(mfrc522.ComIrqReg, 0x7F);
    newCard = false;
  };
}
