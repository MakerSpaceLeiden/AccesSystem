// Node MCU has a weird mapping...

#define LED_GREEN   16 // D0
#define LED_ORANGE  5  // D1
#define RELAY       4  // D2
#define PUSHBUTTON  0  // D3

void setup() {
  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_ORANGE, OUTPUT);
  pinMode(RELAY, OUTPUT);

  pinMode(PUSHBUTTON, INPUT);

  Serial.begin(115200);
  Serial.println("\n\n\n" __FILE__ "/" __DATE__ "/" __TIME__);
}

// the loop function runs over and over again forever
void loop() {
  static unsigned int i = 0;
  digitalWrite(LED_GREEN, i & 1);
  digitalWrite(LED_ORANGE, i & 2);
  digitalWrite(RELAY, i & 4);

  Serial.print("Button: ");
  Serial.println(digitalRead(PUSHBUTTON) ? "not-pressed" : "PRESSED");
  delay(500);
  i++;
}
