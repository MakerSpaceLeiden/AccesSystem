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

#define WHPULS_GPIO      (4)  // digital in - Wh pulse
#define CURRENT_GPIO    (24)  // Analog in - current
#define RELAY_GPIO       (5)  // output

#define AART_LED        (16)  // superfluous indicator LED.

#define REPORTING_PERIOD    (300 * 1000)      // every 5 minutes when on.
#define REPORTING_PERIOD_OFF  (3600 * 1000)   // every hour when we are off.

// #define RFID_SELECT_PIN (6)
// #define RFID_RESET_PIN (7)

#define LWIP_DHCP_GET_NTP_SRV 1
#include "apps/sntp/sntp.h"

#include <ACNode.h>
// #include <MSL.h>
// #include <SIG1.h>
#include <SIG2.h>

#include <Beat.h>
#include <OTA.h>
#include <RFID.h>

#include <SyslogStream.h>
#include <MqttLogStream.h>

#include "/Users/dirkx/.passwd.h"

#ifdef WIFI_NETWORK
ACNode node = ACNode(WIFI_NETWORK, WIFI_PASSWD);
#else
ACNode node = ACNode(true);
#endif

OTA ota = OTA("FooBar");
SIG2 sig2 = SIG2();
Beat beat = Beat();


typedef enum {
  BOOTING, SWERROR, OUTOFORDER, NOCONN, // some error - machine disabLED.
  WAITINGFORCARD,             // waiting for card.
  CHECKINGCARD,
  POWERED,                    // this is where we engage the relay.
  REJECTED,
  POWEROFF,                 // power down an report use.
  NOTINUSE
} machinestates_t;

const char *machinestateName[NOTINUSE] = {
  "Software Error", "Out of order", "No network",
  "Waiting for card",
  "Powered",
  "Rejecting noise/card",
  "Poweroff",
  "== not in use == "
};

unsigned long laststatechange = 0;
static machinestates_t laststate = OUTOFORDER;
machinestates_t machinestate = BOOTING;


unsigned long whCounter = 0, startWhCounter = 0;
void irqWattHourPulse() {
  whCounter++;
}

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


void reportUse(bool isFinal) {
  if (isFinal)
    Log.printf("Final %.1f; by %s\n", (whCounter - startWhCounter) / 1000., "xxx");
  else if (machinestate == POWERED)
    Log.printf("Use %.1f, kWh Counter: %.1f\n", (whCounter - startWhCounter) / 1000., whCounter / 1000.);
  else
    Log.printf("idle, kWh Counter: %.1f\n", whCounter / 1000.);
}


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

  pinMode(CURRENT_GPIO, INPUT);
  pinMode(WHPULS_GPIO, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(WHPULS_GPIO), irqWattHourPulse, FALLING);

#define Strncpy(dst,src) { strncpy(dst,src,sizeof(dst)); }
  Strncpy(_acnode->moi, "test-ceramitcs");
  Strncpy(_acnode->mqtt_server, "space.vijn.org");
  Strncpy(_acnode->machine, "test-ceramitcs");
  Strncpy(_acnode->master, "test-master");
  Strncpy(_acnode->logpath, "log");
  Strncpy(_acnode->mqtt_topic_prefix, "test");

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
      startWhCounter = whCounter;
    }
    else if (!strcasecmp("report", cmd)) {
      reportUse(false);
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
  Debug.addPrintStream(std::make_shared<SyslogStream>(syslogStream));
  Log.addPrintStream(std::make_shared<SyslogStream>(syslogStream));

  // assumes the client connection for MQTT (and network, etc) is up - otherwise silenty fails/buffers.
  //
  MqttLogStream mqttlogStream = MqttLogStream("test", "moi");
  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));

  machinestate = BOOTING;

  node.set_debug(true);
  node.set_debugAlive(true);

  node.begin();
}

void loop() {
  node.loop();

  if (laststate != machinestate) {
    laststate = machinestate;
    laststatechange = millis();
  }

  switch (machinestate) {
    case WAITINGFORCARD:
      digitalWrite(RELAY_GPIO, 0);
      blinkstate = HEARTBEAT;
      break;

    case CHECKINGCARD:
      digitalWrite(AART_LED, ((millis() % 500) < 100) ? 1 : 0);
      blinkstate = SLOW;
      if ((millis() - laststatechange) > 5000)
        machinestate = REJECTED;
      break;

    case POWERED:
      blinkstate = ON;
      digitalWrite(RELAY_GPIO, 1);
      digitalWrite(AART_LED, 1);
      if ((millis() - laststatechange) > 5000)
        machinestate = WAITINGFORCARD;
      break;

    case REJECTED:
      digitalWrite(RELAY_GPIO, 0);
      machinestate = WAITINGFORCARD;
      break;

    case POWEROFF:
      digitalWrite(RELAY_GPIO, 0);
      reportUse(true);
      machinestate = WAITINGFORCARD;
      break;

    case NOCONN:
      blinkstate = FAST;

      if ((millis() - laststatechange) > 120 * 1000) {
        node.delayedReboot();
      }
      break;

    case BOOTING:
    case OUTOFORDER:
    case SWERROR:
    case NOTINUSE:
      blinkstate = FAST;
      break;
  };

  static unsigned long lastReportUse = 0;
  if (millis() - lastReportUse > ((machinestate == POWERED) ? REPORTING_PERIOD : REPORTING_PERIOD_OFF)) {
    reportUse(false);
  };
}

