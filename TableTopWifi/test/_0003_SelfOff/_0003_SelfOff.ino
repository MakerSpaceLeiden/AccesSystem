// Basic test of the device its ability
// to power itself (completey).
//

#define POWEROFF D2 // GPIO4 in ESP nomencalture

void setup() {
  Serial.begin(9600);
  Serial.println("Starting build " __FILE__ "/" __DATE__ "/" __TIME__);
}

void loop() {
  static unsigned long lastTime = 0;
  if (millis() - lastTime > 10000) {
      Serial.println("Lets try off...");

      pinMode(POWEROFF, OUTPUT);
      digitalWrite(POWEROFF, HIGH);   
      delay(10000); 
      lastTime=millis();

      pinMode(POWEROFF, INPUT);
      Serial.println("Released...");
  };                     
  delay(100);                    
}

