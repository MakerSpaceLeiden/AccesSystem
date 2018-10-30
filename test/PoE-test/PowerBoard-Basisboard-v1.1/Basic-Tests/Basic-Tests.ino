// https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1
//
#define AART_LED        (GPIO_NUM_16)
#define RELAY           (GPIO_NUM_5)
#define CURRENT_COIL    (GPIO_NUM_15)
#define SW1             (GPIO_NUM_2)
#define SW2             (GPIO_NUM_39)
#define OPTO1           (GPIO_NUM_34)
#define OPTO12          (GPIO_NUM_35)

void setup() {

  Serial.begin(9600);
  delay(250);
  Serial.print("\n\n\n\nBooting ");
  Serial.println(__FILE__);
  Serial.println(__DATE__ " " __TIME__);

  pinMode(AART_LED, OUTPUT);
  pinMode(RELAY, OUTPUT);
  pinMode(CURRENT_COIL, ANALOG); // analog

#ifdef SW1
  pinMode(SW1, INPUT); 
#endif
#ifdef SW2
  pinMode(SW2, INPUT); 
#endif
}

void loop() {
#ifdef AART_LED
{
    static unsigned long aartLedLastChange = 0;
    static int aartLedState = 0;
    if (millis() - aartLedLastChange > 100) {
      aartLedState = (aartLedState + 1) & 7;
      digitalWrite(AART_LED, aartLedState ? HIGH : LOW);
      aartLedLastChange = millis();
    };
  }
#endif

#ifdef RELAY
  {
    static unsigned long relayLastChange = 0;
    if (millis() - relayLastChange > 1000) {
      digitalWrite(RELAY, !digitalRead(RELAY));
      relayLastChange = millis();
    };
  }
#endif

#ifdef CURRENT_COIL
  {
    static unsigned long lastCurrentMeasure = 0;
    if (millis() - lastCurrentMeasure > 1000) {
      Serial.printf("Current %f\n", analogRead(CURRENT_COIL)/1024.);
      lastCurrentMeasure = millis();
    };
  }
#endif

#ifdef SW2
  {
    static unsigned long last = 0;
    if (digitalRead(SW2) != last) {
      Serial.printf("Current state SW2: %d\n", digitalRead(SW2));
      last = digitalRead(SW2);
    };
  }
#endif
#ifdef SW1
  {
    static unsigned long last = 0;
    if (digitalRead(SW1) != last) {
      Serial.printf("Current state SW1: %d\n", digitalRead(SW1));
      last = digitalRead(SW1);
    };
  }
#endif

#ifdef OPTO1
  {
     static unsigned last = 0; 
     if (millis() - last > 1000) {
      last = millis();
      Serial.printf("OPTO 1: %d\n", analogRead(OPTO1));
     }     
  }
#endif
#ifdef OPTO2
  {
     static unsigned last = 0; 
     if (millis() - last > 1000) {
      last = millis();
      Serial.printf("OPTO 1: %d\n", analogRead(OPTO2));
     }     
  }
#endif
}
