// Switch the big power relay on and off at a steady clip.
//
#define RELAY D8

void setup() {
  Serial.begin(9600);
  Serial.println("Starting build " __FILE__ "/" __DATE__ "/" __TIME__);
  pinMode(RELAY, OUTPUT);   
}

void loop() {
  Serial.println("off");
  digitalWrite(RELAY, LOW);   
  delay(500);                      

  Serial.println("on");
  digitalWrite(RELAY, HIGH); 
  delay(800);                    
}

