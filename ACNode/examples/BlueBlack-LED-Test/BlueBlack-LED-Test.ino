#ifndef ARDUINO_ESP32_WROOM_DA
#error "Black/Blue Hardware is expected to be an ESP32 WROOM-DA"
#endif

#include <ExpandedGPIO.h>
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
constexpr byte I2C_ADDR = 0x28;

void setup() {
  Serial.begin(115200);  // Initialize serial communications with the PC
  delay(1000);           // give the terminal a second to connect, etc after a
  Serial.println("\n\n\nStarting LED/AW expander init....\n\n");

  Wire.begin(I2C_SDA, I2C_SCL, 200000);
  ExpandedGPIO::getInstance().addAW9523();
  // Reduce the current to a sensible level.
  // Awaiting https://github.com/adafruit/Adafruit_AW9523/pull/5.
  //
  Wire.beginTransmission(0x58);
  Wire.write(0x11);
  Wire.write(3);
  Wire.endTransmission();

  xpinMode(LEDA, AW9523_LED_MODE);
  xanalogWrite(LEDA, 0);

  xpinMode(LEDB, AW9523_LED_MODE);
  xanalogWrite(LEDB, 0);

  xpinMode(LEDC, AW9523_LED_MODE);
  xanalogWrite(LEDC, 0);

  xpinMode(LEDD, AW9523_LED_MODE);
  xanalogWrite(LEDD, 0);

  xpinMode(LEDE, AW9523_LED_MODE);
  xanalogWrite(LEDE, 0);

  // used on Olga door red-green LEDs
  xpinMode(IOC, AW9523_LED_MODE);
  xanalogWrite(IOC, 0);
  xpinMode(IOE, AW9523_LED_MODE);
  xanalogWrite(IOE, 0);

  xpinMode(OPTO0, INPUT);
  xpinMode(OPTO1, INPUT);
  xpinMode(OPTO2, INPUT);
  xpinMode(OPTO3, INPUT);

  xpinMode(BUTT0, INPUT_PULLUP);
  xpinMode(BUTT1, INPUT_PULLUP);
  xpinMode(BUTT2, INPUT_PULLUP);

  // used on Olga door - red/green button
  xpinMode(IOD, INPUT_PULLUP);
  xpinMode(IOB, INPUT_PULLUP);

  xpinMode(OUT0, OUTPUT);
  xpinMode(OUT1, OUTPUT);

  Serial.println("Starting loop() with blinkenlights");
}
void loop() {
  static unsigned int i = 0;
  i++;

  {
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
      xanalogWrite(IOC, (j == 5) ? 255 : 0);
      xanalogWrite(IOE, (j == 6) ? 255 : 0);
      
      xdigitalWrite(LED_INDICATOR, j == 5);
    };
  };

  xdigitalWrite(BUZZER, (i >> 3) % 500 < 2);

  {
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
    if (millis() > lst + 1000) {
      lst = millis();
      Serial.printf("Buttons %d, %d, %d, %d, %d\n",
                    xdigitalRead(BUTT0),
                    xdigitalRead(BUTT1),
                    xdigitalRead(BUTT2),
                    xdigitalRead(IOB),
                    xdigitalRead(IOD)
                    );
      Serial.printf("Optos:  %d, %d, %d, %d\n",
                    xdigitalRead(OPTO0),
                    xdigitalRead(OPTO1),
                    xdigitalRead(OPTO2),
                    xdigitalRead(OPTO3));
    };
  };
}
