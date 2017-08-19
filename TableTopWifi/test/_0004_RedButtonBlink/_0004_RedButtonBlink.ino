// The LED of the red button is wired to a transistor; so we can
// make it flash.
//

#define REDLED D3 // GPIO4 in ESP nomencalture

void setup() {
  Serial.begin(9600);
  Serial.println("Starting build " __FILE__ "/" __DATE__ "/" __TIME__);

  pinMode(REDLED, OUTPUT);
}

void loop() {
  digitalWrite(REDLED,!digitalRead(REDLED));
  delay(100);                    
}

