#ifndef ARDUINO_ESP32_WROOM_DA
#error "Black/Blue Hardware is expected to be an ESP32 WROOM-DA"
#endif

#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#include <ExpandedGPIO.h>
#include <util/cufflink_heartbeat.h>
#include "esp_mac.h"  // required - exposes esp_mac_type_t values

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

const byte CURR0 = 36;  // SENSOR_VN
const byte CURR1 = 37;  // SENSOR_VP

// Moved from ESP32 to AW gated IO (PIN_HPIO_AW9523==2<<6= 128
const byte LED_INDICATOR = PIN_HPIO_AW9523 | (8 + 6);  // P1_6 on the AW9523
const byte BUZZER = PIN_HPIO_AW9523 | (8 + 7);         // P1_7 on the AW9523

// Moved from ESP32 to AW gated IO
const byte OPTO0 = PIN_HPIO_AW9523 | (0 + 4);  // P0_4
const byte OPTO1 = PIN_HPIO_AW9523 | (0 + 3);  // P0_3

// Two extra opto couplers, introduced in v1.11
const byte OPTO2 = PIN_HPIO_AW9523 | (0 + 1);  // P0_1
const byte OPTO3 = PIN_HPIO_AW9523 | (0 + 2);  // P0_2

// Extra LEDs on the front, introduced in v1.11
const byte LEDA = PIN_HPIO_AW9523 | (8 + 0);  // P1_0 -- checked on blue board
const byte LEDB = PIN_HPIO_AW9523 | (8 + 2);  // P1_2 -- checked on blue board
const byte LEDC = PIN_HPIO_AW9523 | (8 + 1);  // P1_1 -- checked on blue board
const byte LEDD = PIN_HPIO_AW9523 | (8 + 3);  // P1_3
const byte LEDE = PIN_HPIO_AW9523 | (0 + 0);  // P0_0 -- checked on black & blue board

// Extra connector intruduced with v1.11
const byte IOA = PIN_HPIO_AW9523 | (0 + 5);  // P0_5
const byte IOB = PIN_HPIO_AW9523 | (0 + 6);  // P0_6
const byte IOC = PIN_HPIO_AW9523 | (0 + 7);  // P0_7
const byte IOD = PIN_HPIO_AW9523 | (8 + 4);  // P1_4
const byte IOE = PIN_HPIO_AW9523 | (8 + 5);  // P1_5

const byte BUTT0 = 14;  // Labeled YES on the PCB -- not yet tested.
const byte BUTT1 = 13;  // Labeled NO on the PCB -- not yet tested.
const byte BUTT2 = 0;   // Labeled MENU on the PCB -- not yet tested.

const byte OUT0 = 16;
const byte OUT1 = 04;

constexpr uint8_t I2C_SCL = 15;
constexpr uint8_t I2C_SDA = 5;
constexpr byte EXPANDER_ADDR = 0x58;

// Settings for White and newer
constexpr uint8_t RFID_RST_PIN = 32;  // Configurable, see typical pin layout above
constexpr uint8_t RFID_SCL = 15;
constexpr uint8_t RFID_SDA = 5;
constexpr byte RFID_ADDR = 0x28;

TwoWire i2cBus = TwoWire(0);
MFRC522_I2C dev = MFRC522_I2C(RFID_RST_PIN, RFID_ADDR, i2cBus);
MFRC522 mfrc522 = MFRC522(&dev);


const uint8_t SCREEN_Address = 0x3c;
const uint8_t SCREEN_WIDTH = 128;  // OLED display width, in pixels
const uint8_t SCREEN_HEIGHT = 64;  // OLED display height, in pixels
const uint8_t SCREEN_RESET = -1;   //  Not wired up

Adafruit_SH1106G* display;

String i2cscan(TwoWire& i2cBus) {
  String out;
  int nDevices = 0;

  Serial.println("Scanning...");
  for (int address = 1; address < 127; address++) {
    i2cBus.beginTransmission(address);
    int error = i2cBus.endTransmission();

    if (error == 0 || error == 4) {
      out += " x";
      if (address < 16) out += String("0");
      out += String(address, HEX);
    };
    if (error == 0)
      nDevices++;

    if (error == 4)
      out += "(FAULT)";
  }
  if (nDevices == 0)
    out = String("No I2C found");
  else
    out = String("I2C(") + String(nDevices) + "):" + out;
  return out;
}

String getInterfaceMacAddress(esp_mac_type_t interface) {

  String mac = "";

  unsigned char mac_base[6] = { 0 };

  if (esp_read_mac(mac_base, interface) == ESP_OK) {
    char buffer[18];  // 6*2 characters for hex + 5 characters for colons + 1 character for null terminator
    sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X", mac_base[0], mac_base[1], mac_base[2], mac_base[3], mac_base[4], mac_base[5]);
    mac = buffer;
  }

  return mac;
}

String chip() {
  uint32_t chipId = 0;
  for (int i = 0; i < 17; i = i + 8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  char buff[256];
  snprintf(buff, sizeof(buff), "%08x %s r%d #%d", chipId, ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  return String(buff);
};

String rfidId(MFRC522& mfrc522) {
  char* str;
  unsigned char version = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
  switch (version) {
    case 0x00: str = "00-wiring-error"; break;
    case 0xFF: str = "FF-wiring-error"; break;
    case 0x90: str = "v0.0"; break;
    case 0x91: str = "v1.0"; break;
    case 0x92: str = "v2.0"; break;
    case 0x12: str = "fake"; break;
    case 0x88: str = "clne"; break;
    default: str = "unkn"; break;
  };
  return String("") + String(str) + String(" (0x") + String(version, HEX) + String(")");
};


void setup() {
  xpinMode(BUTT0, OUTPUT);   // XXX for logic analyser
  digitalWrite(BUTT0, LOW);  // XXX for logic analyser

  Serial.begin(115200);  // Initialize serial communications with the PC
  delay(1000);           // give the terminal a second to connect, etc after a
  Serial.println("\n\n\n" __FILE__ "'n" __DATE__ " " __TIME__);

  i2cBus.begin(I2C_SDA, I2C_SCL, 400000);

#if 0
  i2cBus.setTimeout(6000); 
  i2cBus.setClock ( 100000L ) ;
#endif

  String i2c_msg = i2cscan(i2cBus);
  Serial.println(i2c_msg);


  ExpandedGPIO::getInstance().addAW9523(EXPANDER_ADDR, &i2cBus);

  // Reduce the current to a sensible level.
  // Awaiting https://github.com/adafruit/Adafruit_AW9523/pull/5.
  //
  i2cBus.beginTransmission(EXPANDER_ADDR);
  i2cBus.write(0x11);
  i2cBus.write(3);
  i2cBus.endTransmission();

  mfrc522.PCD_Init();                 // Init MFRC522
  mfrc522.PCD_DumpVersionToSerial();  // Show details of PCD - MFRC522 Card Reader details

  xpinMode(LED_INDICATOR, OUTPUT);
  xdigitalWrite(LED_INDICATOR, 1);
  delay(500);
  xdigitalWrite(LED_INDICATOR, 0);

  xpinMode(LEDA, AW9523_LED_MODE);
  xanalogWrite(LEDA, 0);

  xpinMode(LEDB, AW9523_LED_MODE);
  xanalogWrite(LEDB, 0);

  xpinMode(LEDC, AW9523_LED_MODE);
  xanalogWrite(LEDC, 0);
#if 0
  xpinMode(LEDD, AW9523_LED_MODE);
  xanalogWrite(LEDD, 0);

  xpinMode(LEDE, AW9523_LED_MODE);
  xanalogWrite(LEDE, 0);
#endif

  xpinMode(OPTO0, INPUT);
  xpinMode(OPTO1, INPUT);
  xpinMode(OPTO2, INPUT);
  xpinMode(OPTO3, INPUT);

  xpinMode(BUTT0, INPUT_PULLUP);
  xpinMode(BUTT1, INPUT_PULLUP);
  xpinMode(BUTT2, INPUT_PULLUP);

  xpinMode(OUT0, OUTPUT);
  xpinMode(OUT1, OUTPUT);


  display = new Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &i2cBus, SCREEN_RESET, 400000, 100000);
  if (display) {
    Serial.println("We hava found screen");
    display->setRotation(2);
    display->begin(SCREEN_Address, false);
    display->setCursor(0, 0);
    // display->setFont(FONT_SMALL);
    display->setTextSize(1);
    display->setTextColor(SH110X_WHITE);
    display->oled_command(SH110X_DISPLAYON);
    display->clearDisplay();
    display->println(i2c_msg);
    display->display();
  } else {
    Serial.printf("No screen");
  }

  String chip_msg = chip();
  Serial.print("Chip:\n\t");
  Serial.println(chip_msg);
  if (display) {
    display->println(chip_msg);
  };

  Serial.print("Wifi\n  ");
  Serial.println(getInterfaceMacAddress(ESP_MAC_WIFI_STA));
  if (display) {
    display->print("W ");
    display->println(getInterfaceMacAddress(ESP_MAC_WIFI_STA));
  };

  Serial.print("Ethernet\n  ");
  Serial.println(getInterfaceMacAddress(ESP_MAC_ETH));
  if (display) {
    display->print("E ");
    display->println(getInterfaceMacAddress(ESP_MAC_ETH));
  };

  String rfid_msg = rfidId(mfrc522);
  Serial.print("RFID: ");
  Serial.println(rfid_msg);

  if (display) {
    display->print("RFID: ");
    display->println(rfid_msg);
  };

  Serial.print("Starting loop() with blinkenlights in: ");
  for (int i = 9; i; i--) {
    Serial.print(i);
    if (display) {
      display->print(".");
      display->display();
    };
    delay(1000);
  };
};

void loop() {
  static unsigned int i = 0;
  i++;
  if (1) {
    static unsigned long lst = 0;
    static unsigned j = 0;
    if (millis() > lst + 200) {
      lst = millis();
      j = (j + 1) % 8;
      xanalogWrite(LEDA, (j == 0) ? 255 : 0);
      xanalogWrite(LEDB, (j == 1) ? 255 : 0);
      xanalogWrite(LEDC, (j == 2) ? 255 : 0);
      xanalogWrite(LEDD, (j == 3) ? 255 : 0);
      xanalogWrite(LEDE, (j == 4) ? 255 : 0);
    };
  };
  {
    static uint8_t i = 0;
    static unsigned long lst = 0;
    if (millis() > lst + 250) {
      lst = millis();
      digitalWrite(LED_INDICATOR, i % 2);
      i++;
    };
  };

  if (1) {
    static unsigned long lst = 0;
    if (millis() > lst + 1000) {
      lst = millis();
      static bool a;
      a = !a;
      lst = millis();
      xdigitalWrite(OUT0, a);
      xdigitalWrite(OUT1, !a);
    }
  };

  {
    static unsigned long lst = 0;
    if ((millis() > lst + 1000) && (display)) {
      lst = millis();
      display->clearDisplay();
      display->setCursor(0, 0);
      static unsigned long i = 0;
      display->println(++i);
      display->display();
    }


    if (1) {
      static unsigned long lst = 0;
      if ((millis() > lst + 1000)) {
        lst = millis();
        Serial.printf("Buttons %d, %d, %d\n",
                      xdigitalRead(BUTT0),
                      xdigitalRead(BUTT1),
                      xdigitalRead(BUTT2));
        Serial.printf("Optos:  %d, %d, %d, %d\n",
                      xdigitalRead(OPTO0),
                      xdigitalRead(OPTO1),
                      xdigitalRead(OPTO2),
                      xdigitalRead(OPTO3));
        Serial.printf("Current: %3d\n", analogRead(CURR0));
        if (display) {
          display->printf("B:%d,%d,%d O:%d,%d,%d,%d\n",
                          xdigitalRead(BUTT0),
                          xdigitalRead(BUTT1),
                          xdigitalRead(BUTT2),
                          xdigitalRead(OPTO0),
                          xdigitalRead(OPTO1),
                          xdigitalRead(OPTO2),
                          xdigitalRead(OPTO3));
          display->printf("C: %3d", analogRead(CURR0));
          display->display();
        };
      };
    };
    {
      static unsigned long lst = 0;
      if (millis() > lst + 10000) {
        lst = millis();
        xdigitalWrite(BUZZER, HIGH);
        delay(25);
        xdigitalWrite(BUZZER, LOW);
      };
    };


    if (!mfrc522.PICC_IsNewCardPresent()) {
      return;  // no card in sight.
    }

    // Select one of the cards
    if (!mfrc522.PICC_ReadCardSerial()) {
      Serial.println("Bad read (was card removed too quickly?)");
      xdigitalWrite(BUZZER, 1);
      delay(20);
      xdigitalWrite(BUZZER, 0);
      return;
    };

    if (mfrc522.uid.size == 0) {
      Serial.println("Bad card read (size = 0)");
      xdigitalWrite(BUZZER, 1);
      delay(40);
      xdigitalWrite(BUZZER, 0);
      return;
    };

    xdigitalWrite(BUZZER, 1);
    delay(300);
    xdigitalWrite(BUZZER, 0);

    char buff[sizeof(mfrc522.uid.uidByte) * 5 + 1] = { 0 };
    for (int i = 0; i < mfrc522.uid.size; i++) {
      char tag[5];  // 3 digits, dash and \0.
      snprintf(tag, sizeof(tag), "%s%d", i ? "-" : "", mfrc522.uid.uidByte[i]);
      strncat(buff, tag, sizeof(tag));
    };
    Serial.printf("Good scan (len=%d): ", mfrc522.uid.size);
    Serial.println(buff);
    if (display) {
      display->println(buff);
      display->display();
    };

    // disengage with the card.
    //
    mfrc522.PICC_HaltA();
  }
};
