#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>

// Makerspace hardcoded usernames and passwords.
//
#include "../../../../../.passwd.h"

const char* mqtt_server = MQTT_MAKERSPACE_HOST;
const char* topic = "test/woodgrindernode";
#define MOI "HW-TEST-WN"


#define LED 	           0  // LED on the front; in the push/toggle button
#define RELAY           15  // 5A relay that controls the output
#define HALL            A0  // HALL sensor, ACS712 -- analog output with resitor bridge.
#define HALL_RANGE       5  // Range of HALL sensor; 5 Ampere.

// RFID reader
#define RFID

#ifdef RFID
#define RST_PIN         16  // Optional.
#define SDA_PIN         02  // Sometimes called 'SS' for Slave Select
#define MISO_PIN        12  // harcoded for ESP8266
#define MOSI_PIN        13  // harcoded for ESP8266
#define CLK_PIN         14  // harcoded for ESP8266
MFRC522 mfrc522(SDA_PIN, RST_PIN);  // Create MFRC522 instance
#endif


WiFiClient espClient;
PubSubClient client(espClient);

ESP8266WiFiMulti WiFiMulti;
bool rfid_present = false;

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] [");
  for (int i = 0; i < length; i++) {
    char c = payload[i];
    Serial.print((c >= 32 && c < 128) ? c : '.');
  }
  Serial.print("] --- ");
  Serial.print(length);
  Serial.print(" bytes");
  Serial.println();
}

void setup() {
  pinMode(LED, OUTPUT);   // On front; in push button for power
  pinMode(RELAY, OUTPUT); // RELAY that switches main.

  Serial.begin(115200);
  Serial.println("\n\n\n" __FILE__ "/" __DATE__ "/" __TIME__);

#ifdef RFID
  Serial.println("Initializing SPI bus and RFID");
  SPI.begin();
  mfrc522.PCD_Init();
  rfid_present = true; // mfrc522.PCD_PerformSelfTest();
  
  if (rfid_present)
    mfrc522.PCD_DumpVersionToSerial();  // Show details of PCD - MFRC522 Card Reader details
  else
    Serial.println("RFID reader did not pass selftest.");
#else
  Serial.println("RFID reader disabled");
#endif

  WiFiMulti.addAP(WIFI_MAKERSPACE_NETWORK, WIFI_MAKERSPACE_PASSWD);

  Serial.print("Connecting to wifi..");
  while (WiFiMulti.run() != WL_CONNECTED) {
    digitalWrite(LED, !digitalRead(LED));
    Serial.print(".");
    delay(500);
  }
  Serial.println("OK");

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

// the loop function runs over and over again forever
void loop() {
  if (WiFi.status() == WL_CONNECTED) {

    static unsigned long last_connect_attempt = 0;
    if (last_connect_attempt == 0 || ((!client.connected()) && (millis() - last_connect_attempt) > 5000)) {
      last_connect_attempt = millis();
      Serial.print("Connecting to MQTT: ");
      if (client.connect(MOI)) {
        Serial.println("Ok");
        client.publish(topic, "Booted " MOI);
        // client.subscribe("xxx");
      } else {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" and will try again in 5 seconds");
      }
    }
  }
  client.loop();
#ifdef RFID
  if (rfid_present)
    if (mfrc522.PICC_IsNewCardPresent()) {
      char * msg = "New cart detected";
      Serial.println(msg);
      client.publish(topic, msg);

      if (mfrc522.PICC_ReadCardSerial()) {
        char msg[128];
        snprintf(msg, sizeof(msg), "MFRC522: %02x-%02x-%02x-%02x",
                 mfrc522.uid.uidByte[0], mfrc522.uid.uidByte[1], mfrc522.uid.uidByte[2], mfrc522.uid.uidByte[3]);
        Serial.println(msg);
        client.publish(topic, msg);
        //    mfrc522.PICC_DumpToSerial(&(mfrc522.uid));
      }
    }
#endif

  static unsigned long tock = 0;
  if (millis() - tock > 500) {
    static unsigned int i = 0;
    tock = millis();
    digitalWrite(LED, i & 1);
    digitalWrite(RELAY, i & 4);
    float current = ((analogRead(HALL) / 1024) - 0.5) * HALL_RANGE;

    char msg[128];
    snprintf(msg, sizeof(msg), "LED %s, RELAY %s Current %d mAmp, raw %d",
             i & 1 ? "On " : "Off",
             i & 2 ? "On " : "Off",
             (int)(current * 1000), analogRead(HALL));
    Serial.println(msg);
    client.publish(topic, msg);
    i++;
  }
}
