// https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1
#define AART_LED        (GPIO_NUM_16)
#define RELAY           (GPIO_NUM_5)
#define CURRENT_COIL    (GPIO_NUM_15)
#define SW2             (GPIO_NUM_2)

void setup() {
  Serial.begin(115200);
  Serial.print("Booting ");
  Serial.println(__FILE__);
  Serial.println(__DATE__ " " __TIME__);

  // put your setup code here, to run once:
  pinMode(AART_LED, OUTPUT);
  pinMode(RELAY, OUTPUT);
  pinMode(CURRENT_COIL, INPUT); // analog
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
    static unsigned long lastStateSW2 = 0;
    if (digitalRead(SW2) != lastStateSW2) {
      Serial.printf("Current state SW2: %d\n", digitalRead(SW2));
      lastStateSW2 = digitalRead(SW2);
    };
  }
#endif
}
