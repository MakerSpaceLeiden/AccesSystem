/*
      Copyright 2015-2018 Dirk-Willem van Gulik <dirkx@webweaving.org>
                          Stichting Makerspace Leiden, the Netherlands.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#define CURRENT_GPIO    (24)  // Analog in - current
#define RELAY_GPIO       (5)  // output
#define OFF_BUTTON       (4)
#define AART_LED        (16)  // superfluous indicator LED.

#define MAX_IDLE_TIME   (305 * 60 * 1000) // auto power off after 30 minutes of no use.

// #define LWIP_DHCP_GET_NTP_SRV 1
// #include "apps/sntp/sntp.h"

#include <ACNode.h>
// #include <MSL.h>
// #include <SIG1.h>
#include <SIG2.h>

#include <Beat.h>
#include <OTA.h>
#include <RFID.h>

#include <SyslogStream.h>
#include <MqttLogStream.h>
#include <TelnetSerialStream.h>

#include "/Users/dirkx/.passwd.h"

#undef WIFI_NETWORK

#ifdef WIFI_NETWORK
ACNode node = ACNode(WIFI_NETWORK, WIFI_PASSWD);
#else
ACNode node = ACNode(true);
#endif

OTA ota = OTA(OTA_PASSWD);
SIG2 sig2 = SIG2();
Beat beat = Beat();

typedef enum {
  BOOTING, SWERROR, OUTOFORDER, NOCONN, // some error - machine disabLED.
  WAITINGFORCARD,             // waiting for card.
  CHECKINGCARD,
  REJECTED,
  POWERED,                    // this is where we engage the relay.
  RUNNING,
  NOTINUSE
} machinestates_t;

const char *machinestateName[NOTINUSE] = {
  "Booting", "Software Error", "Out of order", "No network",
  "Waiting for card",
  "Checking card",
  "Rejecting noise/card",
  "Powered - but idle",
  "Running"
};

unsigned long laststatechange = 0;
static machinestates_t laststate = OUTOFORDER;
machinestates_t machinestate = BOOTING;

#define SLOW_PERIOD (300)
#define FAST_PERIOD (1000)
typedef enum { NONE, SLOW, HEARTBEAT, FAST, ON } blinkpattern_t;
blinkpattern_t blinkstate = FAST;
Ticker aartLed;


#ifdef  ESP32
// The ESP32 does not yet have automatic PWN when its GPIOs are
// given an analogeWrite(). So we have fake that for now - until
// we can ifdef this out at some Espressif SDK version #.
//
#define LEDC_TIMER_13_BIT  (13)
#define LEDC_BASE_FREQ     (5000)
#define LEDC_CHANNEL_0  (0)

void analogWrite(uint8_t gpio, float value) {
  static int channel = -1;
  if (channel == -1) {
    channel = 0;
    // Setup timer and attach timer to a led pin
    ledcSetup(LEDC_CHANNEL_0, LEDC_BASE_FREQ, LEDC_TIMER_13_BIT);
    ledcAttachPin(gpio, LEDC_CHANNEL_0);
  }

  // calculate duty, 8191 from 2 ^ 13 - 1
  uint32_t duty = ((1 << LEDC_TIMER_13_BIT) - 1) * value;

  // write duty to LEDC
  ledcWrite(channel, duty);
}
#endif

void blink() {
  switch (blinkstate) {
    case NONE:
      digitalWrite(AART_LED, 0);
      break;
    case SLOW:
      digitalWrite(AART_LED, (((int)((millis() / SLOW_PERIOD))) & 1));
      break;
    case HEARTBEAT:
      {
        int ramp = ((int)((millis() / SLOW_PERIOD))) % 30;
        if (ramp > 15)
          ramp = 30 - ramp;
        analogWrite(AART_LED,  (ramp / 15.));
      };
      break;
    case ON:
      digitalWrite(AART_LED, 1);
      break;
    case FAST: // fall throguh to default
    default:
      digitalWrite(AART_LED, (((int)((millis() / FAST_PERIOD))) & 1 ));
      break;
  }
};


void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  // Init the hardware and get it into a safe state.
  //
  pinMode(RELAY_GPIO, OUTPUT);
  digitalWrite(RELAY_GPIO, 0);

  pinMode(AART_LED, OUTPUT);
  digitalWrite(AART_LED, 0);

  pinMode(CURRENT_GPIO, INPUT); // analog input.
  pinMode(OFF_BUTTON, INPUT_PULLUP);

#define Strncpy(dst,src) { strncpy(dst,src,sizeof(dst)); }

  Strncpy(_acnode->mqtt_server, "space.makerspaceleiden.nl");

  Strncpy(_acnode->mqtt_topic_prefix, "test");
  Strncpy(_acnode->logpath, "log");

  Strncpy(_acnode->moi, "test-woodlathe");
  Strncpy(_acnode->machine, "test-woodlathe");
  Strncpy(_acnode->master, "test-master");


  aartLed.attach(100, blink);

  node.onConnect([]() {
    Log.println("Connected");
    machinestate = WAITINGFORCARD;
  });
  node.onDisconnect([]() {
    Log.println("Disconnected");
    machinestate = NOCONN;
  });
  node.onError([](acnode_error_t err) {
    Log.printf("Error %d\n", err);
    machinestate = WAITINGFORCARD;
  });

  node.onValidatedCmd([](const char *cmd, const char *restl) {
    if (!strcasecmp("energize", cmd)) {
      machinestate = POWERED;
    }
    else if (!strcasecmp("denied", cmd)) {
      machinestate = REJECTED;
    } else {
      Log.printf("Unhandled command <%s> -- ignored.", cmd);
    }
  });
  node.addHandler(&ota);

#ifdef RFID_SELECT_PIN
  RFID reader(RFID_SELECT_PIN, RFID_RESET_PIN);
  reader.onSwipe([](const char * tag) {
    Log.printf("Card <%s> wiped - being checked.\n", tag);
    machinestate = CHECKINGCARD;
  });
  node.addHandler(reader);
#endif

  // SIG1 sig1 = SIG1();
  // node.addSecurityHandler(SIG1());
  node.addSecurityHandler(&sig2);
  node.addSecurityHandler(&beat);

  // default syslog port and destination (gateway address or broadcast address).
  //
  SyslogStream syslogStream = SyslogStream();
#ifdef SYSLOG_HOST
  syslogStream.setDestination(SYSLOG_HOST);
  syslogStream.setRaw(true);
#ifdef SYSLOG_PORT
  syslogStream.setPort(SYSLOG_PORT);
#endif
#endif
  // Debug.addPrintStream(std::make_shared<SyslogStream>(syslogStream));
  Log.addPrintStream(std::make_shared<SyslogStream>(syslogStream));

  // assumes the client connection for MQTT (and network, etc) is up - otherwise silenty fails/buffers.
  //
  MqttLogStream mqttlogStream = MqttLogStream("test", "moi");
  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));

#if 0
  // As the PoE devices have their own grounding - the cannot readily be connected
  // to with a sericd Peral cable.  This allows for a telnet instead.
  TelnetSerialStream telnetSerialStream = TelnetSerialStream();
  auto t = std::make_shared<TelnetSerialStream>(telnetSerialStream);

  Log.addPrintStream(t);
  Debug.addPrintStream(t);
#endif

  machinestate = BOOTING;

  // node.set_debug(true);
  // node.set_debugAlive(true);

  node.begin();
  
  Debug.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
  // secrit reset button.
  pinMode(2, INPUT_PULLUP);
  if (digitalRead(2) == LOW) {
    extern void wipe_eeprom();
    Log.println("Wiped EEPROM");
    wipe_eeprom();
  }
}

void loop() {
  node.loop();
  blink();

  if (analogRead(CURRENT_GPIO) > 512) {
    if (machinestate < POWERED) {
      Log.printf("Error -- device in state '%s' but current detected!",
                 machinestateName[machinestate]);
    }
    if (machinestate == POWERED) {
      machinestate = RUNNING;
      Log.printf("Machine running.");
    }
  } else if (machinestate == RUNNING) {
    machinestate = POWERED;
    Log.printf("Machine halted.");
  }

  // We do not worry much about button bounce; as it is just an off
  // we want to detect. But we'll go to some length to detect a
  // lengthy anomaly - as to not fill up the logs iwth one report/second.
  //
  if (digitalRead(OFF_BUTTON) == LOW) {
    static machinestates_t lastReport = NOTINUSE;
    static unsigned long lastReportTime = 0;
    static unsigned long reportDelay = 5000;
    if (lastReport != machinestate || (millis() - lastReportTime > reportDelay)) {
      lastReport = machinestate;
      if (lastReport != machinestate)
        reportDelay = 5000;
      else if (reportDelay < 10 * 60 * 1000)
        reportDelay *= 2;
      lastReportTime = millis();
      if (machinestate == RUNNING) {
        Log.printf("Machine switched off with button while running (not so nice)\n");
      } else if (machinestate == POWERED) {
        Log.printf("Machine switched off with button (normal)\n");;
      } else {
        Log.printf("Off button pressed (currently in state %s)\n",
                   machinestateName[machinestate]);
      }

    }
    machinestate = WAITINGFORCARD;
  };

  if (laststate != machinestate) {
    Log.printf("Changing from state %s to state %s)\n",
               machinestateName[laststate], machinestateName[machinestate]);
    laststate = machinestate;
    laststatechange = millis();
  }

  if (laststate < POWERED)
    digitalWrite(RELAY_GPIO, 0);
  else
    digitalWrite(RELAY_GPIO, 1);

  if (machinestate >= POWERED) {
    blinkstate = ON;
    digitalWrite(AART_LED, 1);
  }

  switch (machinestate) {
    case WAITINGFORCARD:
      blinkstate = HEARTBEAT;
      break;

    case CHECKINGCARD:
      digitalWrite(AART_LED, ((millis() % 500) < 100) ? 1 : 0);
      blinkstate = SLOW;
      if ((millis() - laststatechange) > 5000)
        machinestate = REJECTED;
      break;

    case POWERED:
      if ((millis() - laststatechange) > MAX_IDLE_TIME) {
        Log.printf("Machine idle for too long - switching off..\n");
        machinestate = WAITINGFORCARD;
      }
      break;

    case RUNNING:
      break;

    case REJECTED:
      blinkstate = FAST;
      if ((millis() - laststatechange) > 1000)
        machinestate = WAITINGFORCARD;
      break;

    case NOCONN:
      blinkstate = FAST;

      if ((millis() - laststatechange) > 120 * 1000) {
        Log.printf("Connection to SSID:%s lost for 120 seconds now -- Rebooting...\n", WiFi.SSID().c_str());
        delay(500);
        ESP.restart();
      }
      break;

    case BOOTING:
    case OUTOFORDER:
    case SWERROR:
    case NOTINUSE:
      blinkstate = FAST;
      break;
  };
}

