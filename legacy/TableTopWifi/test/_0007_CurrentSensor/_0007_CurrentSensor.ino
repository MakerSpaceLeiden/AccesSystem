// Output what the current sensor measures.
// Very loosely calibrated !
//

#define CURRENT_SENSOR A0
#define RELAY D8

void setup() {
  Serial.begin(9600);
  Serial.println("Starting build " __FILE__ "/" __DATE__ "/" __TIME__);
  digitalWrite(RELAY,0);
  pinMode(RELAY,OUTPUT);
}

void loop() {
  float raw = analogRead(CURRENT_SENSOR);
  float watt = raw / 2.18;
  Serial.print("Value:\t");
  Serial.print(raw);
  Serial.print(" [raw]\t");
  Serial.print(watt);
  Serial.println(" [Watt]");
  delay(500);             
  digitalWrite(RELAY, (millis() >> 11) & 1);
}

