// Basic WEMOS D1 mini board test.

void setup() {
  Serial.begin(9600);
  Serial.println("Starting build " __FILE__ "/" __DATE__ "/" __TIME__);
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  Serial.println("tick ");
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);

  Serial.println("tock ");
  digitalWrite(LED_BUILTIN, HIGH);
  delay(800);
}

