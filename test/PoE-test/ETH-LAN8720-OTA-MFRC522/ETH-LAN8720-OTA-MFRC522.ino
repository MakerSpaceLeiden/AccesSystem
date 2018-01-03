/*
    This sketch shows the Ethernet event usage
    First light Aart 10-12-2017

*/

#define ETH_PHY_ADDR      1
#define ETH_PHY_MDC       23
#define ETH_PHY_MDIO      18
#define ETH_PHY_POWER     17
#define ETH_PHY_TYPE      ETH_PHY_LAN8720

// Labeling as on the 'red' RFID MFRC522 boards and numbers
// as per labels on the back of PoE board v1.00.
//
// See also page 525 of https://pc7x.net/archive/misc/ESP32_Programming.pdf
//
#define MFRC522_SCK     (14)
#define MFRC522_MISO    (12)
#define MFRC522_MOSI    (13)
#define MFRC522_SS      (15)
#define MFRC522_3V3     /* 3v3 */
#define MFRC522_GND     /* gnd pin */
#define MFRC522_IRQ     (33)
#define MFRC522_RSTO    (32)

#include <ETH.h>
#include <ArduinoOTA.h>

#include <SPI.h>
#include <MFRC522.h>    // Requires modifed MFRC522 (see pull rq) or the -master branch as of late DEC 2017.

SPIClass spirfid = SPIClass(VSPI);
const SPISettings spiSettings = SPISettings(SPI_CLOCK_DIV4, MSBFIRST, SPI_MODE0);
MFRC522 mfrc522(MFRC522_SS, MFRC522_RSTO, &spirfid, spiSettings);

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

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
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
  spirfid.begin(MFRC522_SCK, MFRC522_MISO, MFRC522_MOSI, MFRC522_SS);

  Serial.printf("MFRC522 init SPI=%p spi=%p setting=%d/%d/%d\n", &SPI, &spirfid, spiSettings._clock, spiSettings._bitOrder, spiSettings._dataMode);
  mfrc522.PCD_Init();   // Init MFRC522

  Serial.println("MFRC522 dump version");
  mfrc522.PCD_DumpVersionToSerial();  // Show details of PCD - MFRC522 Card Reader details

  Serial.println("setup() done");
}


void loop()
{
  if (eth_connected) {
    ArduinoOTA.handle();
    if (!ota)
      enableOTA();

  }
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
  //
#if 0
  static byte i;
  pinMode(MFRC522_SS, OUTPUT);
  spirfid.beginTransaction(spiSettings);
  digitalWrite(MFRC522_SS, LOW);
  spirfid.transfer(i++);
  digitalWrite(MFRC522_SS, HIGH);
  spirfid.endTransaction(); // Stop using the SPI bus
  return;
#endif

  if ( ! mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  // Select one of the cards
  if ( ! mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  // Dump debug info about the card; PICC_HaltA() is automatically called
  mfrc522.PICC_DumpToSerial(&(mfrc522.uid));
}
