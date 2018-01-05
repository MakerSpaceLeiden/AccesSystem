/* Spacedeur 2 - 'as is' configuration which should be near identical
    to the existing setup late 2017.
*/

// Wired ethernet.
//
#define ETH_PHY_ADDR      1
#define ETH_PHY_MDC       23
#define ETH_PHY_MDIO      18
#define ETH_PHY_POWER     17
#define ETH_PHY_TYPE      ETH_PHY_LAN8720

// Labelling as per `blue' RFID MFRC522 - MSL 1471 'fixed'
//
#define MFRC522_SDA     (15)
#define MFRC522_SCK     (14)
#define MFRC522_MOSI    (13)
#define MFRC522_MISO    (12)
#define MFRC522_IRQ     (33)
#define MFRC522_GND     /* gnd pin */
#define MFRC522_RSTO    (32)
#define MFRC522_3V3     /* 3v3 */

// Stepper motor - Pololu / A4988
//
#define STEPPER_DIR       (2)
#define STEPPER_ENABLE    (4)
#define STEPPER_STEP      (5)

#define DOOR_CLOSED       (0)
#define DOOR_OPEN         (1100)
#define DOOR_OPEN_DELAY   (10*1000)

#define REPORTING_PERIOD  (300*1000)

typedef enum doorstates { CLOSED, OPENING, OPEN, CLOSING } doorstate_t;
doorstate_t doorstate;
unsigned long long last_doorstatechange = 0;

long cnt_cards = 0, cnt_opens = 0, cnt_closes  = 0, cnt_fails = 0, cnt_misreads = 0, cnt_minutes = 0, cnt_reconnects = 0;

#include <ETH.h>
#include <SPI.h>

#include <ArduinoOTA.h>
#include <AccelStepper.h>
#include <PubSubClient.h>
#include <MFRC522.h>    // Requires modifed MFRC522 (see pull rq) or the -master branch as of late DEC 2017.
// https://github.com/miguelbalboa/rfid.git

SPIClass spirfid = SPIClass(VSPI);
const SPISettings spiSettings = SPISettings(SPI_CLOCK_DIV4, MSBFIRST, SPI_MODE0);
MFRC522 mfrc522(MFRC522_SDA, MFRC522_RSTO, &spirfid, spiSettings);

// Simple overlay of the AccelStepper that configures for the A4988
// driver of a 4 wire stepper - including the additional enable wire.
//
class PololuStepper : public AccelStepper
{
  public:
    PololuStepper(uint8_t step_pin = 0xFF, uint8_t dir_pin = 0xFF, uint8_t enable_pin = 0xFF);
};

PololuStepper::PololuStepper(uint8_t step_pin, uint8_t dir_pin, uint8_t enable_pin)
  : AccelStepper(AccelStepper::DRIVER, step_pin, dir_pin)
{
  pinMode(STEPPER_ENABLE, OUTPUT);
  digitalWrite(STEPPER_ENABLE, HIGH); // dis-able stepper first.
  setEnablePin(enable_pin);
}

PololuStepper stepper = PololuStepper(STEPPER_STEP, STEPPER_DIR, STEPPER_ENABLE);

const char mqtt_host[] = "space.vijn.org";
const unsigned short mqtt_port = 1883;

#define PREFIX "test/"

const char rfid_topic[] = PREFIX "deur/space2/rfid";
const char door_topic[] = PREFIX "deur/space2/open";
const char log_topic[] = PREFIX "log";

WiFiClient wifiClient;
PubSubClient client(wifiClient);

static bool eth_connected = false;

char pname[128] = "some-unconfigured-door";

static bool ota = false;
void enableOTA() {
  if (ota)
    return;

  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  ArduinoOTA.setHostname(pname);

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

    char buff[256];
    snprintf(buff, sizeof(buff), "[%s] %s OTA re-programming started.", pname, type.c_str());

    Serial.println(buff);
    client.publish(log_topic, buff);
    client.loop();

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
  });

  ArduinoOTA.onEnd([]() {
    char buff[256];
    snprintf(buff, sizeof(buff), "[%s] OTA re-programming completed. Rebooting.", pname);

    Serial.println(buff);
    client.publish(log_topic, buff);
    client.loop();

    client.disconnect();
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
    static int lp = -1 ;
    int p = progress / (total / 10);
    if (p != lp) Serial.printf("Progress: %u%%\n", p * 10);
    lp = p;
  });

  // Unfortunately - deep in OTA it auto defaults to Wifi. So we
  // force it to ETH -- requires pull RQ https://github.com/espressif/arduino-esp32/issues/944
  // and https://github.com/espressif/esp-idf/issues/1431.
  //
  ArduinoOTA.begin(TCPIP_ADAPTER_IF_ETH);

  Serial.println("OTA enabled.");
  ota = true;
}

String DisplayIP4Address(IPAddress address)
{
  return String(address[0]) + "." +
         String(address[1]) + "." +
         String(address[2]) + "." +
         String(address[3]);
}

String connectionDetailsString() {
  return "Wired Ethernet: " + ETH.macAddress() +
         ", IPv4: " + DisplayIP4Address(ETH.localIP()) + ", " +
         ((ETH.fullDuplex()) ? "full" : "half") + "-duplex, " +
         String(ETH.linkSpeed()) + " Mbps.";
}

void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      Serial.println("ETH Started");
      ETH.setHostname(pname);
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
    case SYSTEM_EVENT_STA_GOT_IP:
      Serial.println(connectionDetailsString());
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

volatile boolean irqCardSeen = false;

void readCard() {
  irqCardSeen = true;
}

/* The function sending to the MFRC522 the needed commands to activate the reception
*/
void activateRec(MFRC522 mfrc522) {
  mfrc522.PCD_WriteRegister(mfrc522.FIFODataReg, mfrc522.PICC_CMD_REQA);
  mfrc522.PCD_WriteRegister(mfrc522.CommandReg, mfrc522.PCD_Transceive);
  mfrc522.PCD_WriteRegister(mfrc522.BitFramingReg, 0x87);
}

/*  The function to clear the pending interrupt bits after interrupt serving routine
*/
void clearInt(MFRC522 mfrc522) {
  mfrc522.PCD_WriteRegister(mfrc522.ComIrqReg, 0x7F);
}



static long lastReconnectAttempt = 0;

boolean reconnect() {
  if (!client.connect(pname)) {
    // Do not log this to the MQTT bus - as it may have been us posting too much
    // or some other loop-ish thing that triggered our disconnect.
    //
    Serial.println("Failed to reconnect to MQTT bus.");
    return false;
  }

  char buff[256];
  snprintf(buff, sizeof(buff), "[%s] %sconnected, %s", pname, cnt_reconnects ? "re" : "", connectionDetailsString().c_str());
  client.publish(log_topic, buff);
  client.subscribe(door_topic);
  Serial.println(buff);

  cnt_reconnects++;

  return client.connected();
}

void setup()
{
  const char * f = __FILE__;
  char * p = rindex(f, '/');
  if (p)
    strncpy(pname, p + 1, sizeof(pname));

  p = index(pname, '.');
  if (p)
    *p = 0;

  Serial.begin(115200);
  Serial.print("\n\n\n\nStart ");
  Serial.print(pname);
  Serial.println(" " __DATE__ " " __TIME__ );

  WiFi.onEvent(WiFiEvent);

  ETH.begin();

  Serial.println("SPI init");
  spirfid.begin(MFRC522_SCK, MFRC522_MISO, MFRC522_MOSI, MFRC522_SDA);

  Serial.println("MFRC522 IRQ and callback setup.");
  mfrc522.PCD_Init();   // Init MFRC522
  mfrc522.PCD_DumpVersionToSerial();  // Show details of PCD - MFRC522 Card Reader details

  pinMode(MFRC522_IRQ, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(MFRC522_IRQ), readCard, FALLING);

  byte regVal = 0xA0; //rx irq
  mfrc522.PCD_WriteRegister(mfrc522.ComIEnReg, regVal);

  Serial.println("Setting up MQTT");
  client.setServer(mqtt_host, mqtt_port);
  client.setCallback(callback);

  Serial.println("Setup of Stepper motor");
  stepper.setMaxSpeed(100);  // divide by 3 to get rpm
  stepper.setAcceleration(80);
  stepper.moveTo(DOOR_CLOSED);

  stepper.run();
  doorstate = CLOSED;
  last_doorstatechange = millis();

  Serial.println("setup() done.\n\n");
}


void callback(char* topic, byte * payload, unsigned int length) {
  char buff[256];

  if (strcmp(topic, door_topic)) {
    Serial.printf("Received an unexepcted %d byte message on topic <%s>, ignoring.", length, topic);
    // We intentinally do not log this message to a MQTT channel - as to reduce the
    // risk of (amplification) loops due to a misconfiguration. We do increase the counter
    // so indirectly this does show up in the MQTT log.
    //
    cnt_fails ++;
    return;
  };

  int l = 0;
  for (int i = 0; l < sizeof(buff) - 1 && i < length; i++) {
    char c = payload[i];
    if (c >= 32 && c < 128)
      buff[l++] = c;
  };
  buff[l] = 0;

  if (!strcmp(buff, "open")) {
    doorstate = OPENING;
    Serial.println("Opening door.");

    stepper.moveTo(DOOR_OPEN);
    return;
  };

  snprintf(buff, sizeof(buff), "[%s] Cannot parse reply <%s> [len = %d, payload len = %d] or denied access.",
           pname, buff, l, length);

  client.publish(log_topic, buff);
  Serial.println(buff);

  cnt_fails ++;
}

void loop()
{
  bool is_moving = stepper.run();

  if (eth_connected) {
    if (ota)
      ArduinoOTA.handle();
    else
      enableOTA();

    if (!client.connected()) {
      long now = millis();
      if (now - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = now;
        if (reconnect()) {
          lastReconnectAttempt = 0;
        }
      }
    } else {
      client.loop();
    }
  } else {
    if (client.connected())
      client.disconnect();
  }
  static unsigned long tock = 0;

  static doorstate_t lastdoorstate = CLOSED;
  switch (doorstate) {
    case OPENING:
      if (!is_moving) {
        char msg[256];
        snprintf(msg, sizeof(msg), "[%s] Door is open.", pname);
        Serial.println(msg);
        client.publish(log_topic, msg);
        doorstate = OPEN;
        cnt_opens++;
      };
      break;
    case OPEN:
      if (millis() - last_doorstatechange > DOOR_OPEN_DELAY) {
        Serial.println("Closing door.");
        stepper.moveTo(DOOR_CLOSED);
        doorstate = CLOSING;
      };
    case CLOSING:
      if (!is_moving) {
        char msg[256];
        snprintf(msg, sizeof(msg), "[%s] Door is closed.", pname);
        Serial.println(msg);
        client.publish(log_topic, msg);
        doorstate = CLOSED;
        cnt_closes++;
      };
      break;
    case CLOSED:
    default:
      break;
  };

  if (lastdoorstate != doorstate) {
    lastdoorstate = doorstate;
    last_doorstatechange = millis();
  }

  if (millis() - tock  > REPORTING_PERIOD) {
    char buff[1024];
    cnt_minutes += ((millis() - tock) + 500) / 1000 / 60;

    snprintf(buff, sizeof(buff),
             "[%s] alive - uptime %02ld:%02ld: "
             "swipes %ld, opens %ld, closes %ld, fails %ld, mis-swipes %ld, mqtt reconnects %ld",
             pname, cnt_minutes / 60, (cnt_minutes % 60),
             cnt_cards,
             cnt_opens, cnt_closes, cnt_fails, cnt_misreads, cnt_reconnects);
    client.publish(log_topic, buff);
    Serial.println(buff);
    tock = millis();
  }

  if (irqCardSeen) {
    if (mfrc522.PICC_ReadCardSerial()) {
      MFRC522::Uid uid = mfrc522.uid;
      cnt_cards++;

      String uidStr = "";
      for (int i = 0; i < uid.size; i++) {
        if (i) uidStr += " - ";
        uidStr += String(uid.uidByte[i], DEC);
      };
      client.publish(rfid_topic, uidStr.c_str());

      char msg[256];
      snprintf(msg, sizeof(msg), "[%s] Tag <%s> (len=%d) swiped", pname, uidStr.c_str(), uid.size);
      client.publish(log_topic, msg);
    } else {
      Serial.println("Misread.");
      cnt_misreads++;
    }
    mfrc522.PICC_HaltA(); // Stop reading

    clearInt(mfrc522);
    irqCardSeen = false;
  };

  // Re-arm/retrigger the scanning regularly.
  {
    static unsigned long reminderToRead = 0;
    if (millis() - reminderToRead > 100) {
      activateRec(mfrc522);
      reminderToRead = millis();
    }
  }
}
