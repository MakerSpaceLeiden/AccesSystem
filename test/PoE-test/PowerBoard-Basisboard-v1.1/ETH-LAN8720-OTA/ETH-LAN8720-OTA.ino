/*
    This sketch shows the Ethernet event usage
    First light Aart 10-12-2017

*/

#define ETH_PHY_ADDR      1
#define ETH_PHY_MDC       23
#define ETH_PHY_MDIO      18
#define ETH_PHY_POWER     17
#define ETH_PHY_TYPE      ETH_PHY_LAN8720

#define TRIAC           (GPIO_NUM_4)
#define RELAY           (GPIO_NUM_5)
#define AART_LED        (GPIO_NUM_16)

#include <ETH.h>
#include <ArduinoOTA.h>

static bool eth_connected = false;

static bool ota = false;
void enableOTA() {
  if (ota)
    return;

  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  ArduinoOTA.setHostname("V1.1-board");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");


  ArduinoOTA
  .onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  })
  .onEnd([]() {
    Serial.println("\nEnd");
  })
  .onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  })
  .onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  // Unfortunately - deep in OTA it auto defaults to Wifi. So we
  // force it to ETH -- requires pull RQ https://github.com/espressif/arduino-esp32/issues/944
  // and https://github.com/espressif/esp-idf/issues/1431.
  //
  ArduinoOTA.begin(); // TCPIP_ADAPTER_IF_ETH);
  ota = true;

  Serial.println("\nOTA enabled too\n");
}

void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      Serial.println("ETH Started");
      //set eth hostname here
      ETH.setHostname("esp32-ethernet");
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
    case SYSTEM_EVENT_STA_GOT_IP:
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_connected = true;
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case SYSTEM_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    default:
      Serial.printf("Unknown event %d\n", event);
      break;
  }
}

void testClient(const char * host, uint16_t port)
{
  Serial.print("\nconnecting to ");
  Serial.println(host);

  WiFiClient client;
  if (!client.connect(host, port)) {
    Serial.println("connection failed");
    return;
  }
  client.printf("GET / HTTP/1.1\r\nHost: %s\r\n\r\n", host);
  while (client.connected() && !client.available());
  while (client.available()) {
    Serial.write(client.read());
  }

  Serial.println("closing connection\n");
  client.stop();
}

void setup()
{
  Serial.begin(115200);
  Serial.println("Start");

  Serial.println("Init");
  WiFi.onEvent(WiFiEvent);

  Serial.println("begin");
  ETH.begin();

  Serial.println("done");

  pinMode(RELAY,OUTPUT);
  pinMode(AART_LED,OUTPUT);
  pinMode(TRIAC,OUTPUT);
  digitalWrite(RELAY,HIGH);
  digitalWrite(AART_LED,HIGH);
  digitalWrite(TRIAC,HIGH);
}


void loop()
{
  if (eth_connected) {
    ArduinoOTA.handle();
    if (!ota)
      enableOTA();

  }

  static unsigned long tock = 0;
  if (millis() - tock  > 1500) {
    Serial.println("Tock");
    tock = millis();
  };

  static unsigned long google = 0;
  if (eth_connected && millis() - google > 10 * 1000) {
    testClient("google.com", 80);
    google = millis();
  }

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


#ifdef TRIAC
  {
    static unsigned long lastChange = 0;
    if (millis() - lastChange > 1000) {
      digitalWrite(TRIAC, !digitalRead(TRIAC));
      lastChange = millis();
    };
  }
#endif

#ifdef CURRENT_COIL
  {
    static unsigned long lastCurrentMeasure = 0;
    if (millis() - lastCurrentMeasure > 1000) {
      Serial.printf("Current %f\n", analogRead(CURRENT_COIL) / 1024.);
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
