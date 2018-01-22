/* SPI - sends an ever increasing count from MO->SI.
 * 
 */

#define ETH_PHY_ADDR      1
#define ETH_PHY_MDC       23
#define ETH_PHY_MDIO      18
#define ETH_PHY_POWER     17
#define ETH_PHY_TYPE      ETH_PHY_LAN8720

#define SPI__SCK     (14)
#define SPI__MISO    (12)
#define SPI__MOSI    (13)
#define SPI__SS      (15)

#include <ETH.h>
#include <ArduinoOTA.h>
#include <SPI.h>

SPIClass spirfid = SPIClass(VSPI);

static bool eth_connected = false;

static bool ota = false;
void enableOTA() {
  if (ota)
    return;

  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  // ArduinoOTA.setHostname("myesp32");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");


  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
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
  ArduinoOTA.begin(TCPIP_ADAPTER_IF_ETH);
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
      Serial.printf("Mbps (event %d)\n", event);
      eth_connected = true;
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Serial.printf("ETH Disconnected (event %d)\n", event);
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
  const char  * pname = rindex(__FILE__, '/');

  Serial.begin(115200);
  Serial.print("\n\n\nStart ");
  Serial.print(pname);
  Serial.println(" " __DATE__ " " __TIME__);

  WiFi.onEvent(WiFiEvent);

  ETH.begin();

  Serial.println("SPI init");
  pinMode(SPI__SS, OUTPUT);
  digitalWrite(SPI__SS,HIGH);

  spirfid.begin(SPI__SCK, SPI__MISO, SPI__MOSI, SPI__SS);

  Serial.println("setup() done");
}


void loop()
{
  if (eth_connected) {
    if (!ota)
      enableOTA();
    ArduinoOTA.handle();
  }

  // The crux of this test -- send an every increasing count to the Slave In.
  //
  static byte i;
  pinMode(SPI__SS, OUTPUT);
  digitalWrite(SPI__SS,LOW);
  spirfid.transfer(i++); 
  digitalWrite(SPI__SS,HIGH);

#if 0
for(int i=0; i < 20;i++){
  {
    const int pin = SPI__MOSI;
    pinMode(pin, OUTPUT);
    static byte i;
    digitalWrite(pin, i % 2); i++;
  }
  {
    const int pin = SPI__MISO;
    pinMode(pin, OUTPUT);
    static byte i;
    digitalWrite(pin, i % 4); i++;
  }
  {
    const int pin = SPI__SS;
    pinMode(pin, OUTPUT);
    static byte i;
    digitalWrite(pin, i % 8); i++;
  }
  {
    const int pin = SPI__SCK;
    pinMode(pin, OUTPUT);
    static byte i;
    digitalWrite(pin, i % 16); i++;
  }
}
#endif

  static unsigned long tock = 0;
  if (millis() - tock  > 30 * 1000) {
    Serial.println("Tock - 30 seconds");
    tock = millis();
  }

#if 0
  static unsigned long google = 0;
  if (eth_connected && millis() - google > 60 * 1000) {
    testClient("google.com", 80);
    google = millis();
  }
#endif
}
