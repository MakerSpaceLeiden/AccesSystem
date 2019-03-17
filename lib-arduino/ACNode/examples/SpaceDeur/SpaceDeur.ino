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
#include <PowerNodeV11.h>
#include <ACNode.h>
#include <RFID.h>   // SPI version
#include <AccelStepper.h>

#define MACHINE             "spacedeur"

// Stepper motor-Pololu / A4988
//
#define STEPPER_DIR       (2)
#define STEPPER_ENABLE    (4)
#define STEPPER_STEP      (5)

#define STEPPER_MAXSPEED  (1850)
#define STEPPER_ACCELL    (850)

#define BUZ_CHANNEL       (0)
#define BUZZER_GPIO       (16) // oude aartled

// Max time to let the stepper move with the stepper
#define MAXMOVE_DELAY     (30*1000)

#define DOOR_CLOSED       (0)
#define DOOR_OPEN         (1100)
#define DOOR_OPEN_DELAY   (10*1000)

// See https://mailman.makerspaceleiden.nl/mailman/private/deelnemers/2019-February/019837.html
//
#define DOOR_SENSOR       (34)
#define DOOR_IS_OPEN      (LOW)
// #define AARTLED_GPIO      (16) // weggehaald, maar 2019, Lucas

#define BUZZ_TIME (5 * 1000) // Buzz 8 seconds.

ACNode node = ACNode(MACHINE);
RFID reader = RFID();
// LED aartLed = LED();    // defaults to the aartLed - otherwise specify a GPIO.

MqttLogStream mqttlogStream = MqttLogStream();
TelnetSerialStream telnetSerialStream = TelnetSerialStream();


// Simple overlay of the AccelStepper that configures for the A4988
// driver of a 4 wire stepper-including the additional enable wire.
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
  digitalWrite(STEPPER_ENABLE, LOW); // dis-able stepper first.
  setPinsInverted(false, false, true); // The enable pin is NOT inverted. Kind of unusual.
  setEnablePin(enable_pin);
}

PololuStepper stepper = PololuStepper(STEPPER_STEP, STEPPER_DIR, STEPPER_ENABLE);


#ifdef OTA_PASSWD
OTA ota = OTA(OTA_PASSWD);
#endif

typedef enum {
  BOOTING, OUTOFORDER,      // device not functional.
  REBOOT,                   // forcefull reboot
  TRANSIENTERROR,           // hopefully goes away level error
  NOCONN,                   // sort of fairly hopless (though we can cache RFIDs!)
  WAITINGFORCARD,           // waiting for card.
  CHECKINGCARD,
  REJECTED,
  START_OPEN,
  OPENING,
  OPEN,
  START_CLOSE,
  CLOSING,
} machinestates_t;

#define NEVER (0)

struct {
  const char * label;                   // name of this state
  LED::led_state_t ledState;            // flashing pattern for the aartLED. Zie ook https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1.
  time_t maxTimeInMilliSeconds;         // how long we can stay in this state before we timeout.
  machinestates_t failStateOnTimeout;   // what state we transition to on timeout.
  unsigned long timeInState;
  unsigned long timeoutTransitions;
  unsigned long autoReportCycle;
} state[CLOSING + 1] =
{
  { "Booting",              LED::LED_ERROR,           120 * 1000, REBOOT,         0 },
  { "Out of order",         LED::LED_ERROR,           120 * 1000, REBOOT,         5 * 60 * 1000 },
  { "Rebooting",            LED::LED_ERROR,           120 * 1000, REBOOT,         0 },
  { "Transient Error",      LED::LED_ERROR,             5 * 1000, WAITINGFORCARD, 5 * 60 * 1000 },
  { "No network",           LED::LED_FLASH,                NEVER, NOCONN,         0 },
  { "Waiting for card",     LED::LED_IDLE,                 NEVER, WAITINGFORCARD, 0 },
  { "Checking card",        LED::LED_PENDING,           5 * 1000, WAITINGFORCARD, 0 },
  { "Rejected",             LED::LED_ERROR,             2 * 1000, WAITINGFORCARD, 0 },
  { "Start opening door",   LED::LED_ON,           MAXMOVE_DELAY, START_CLOSE, 0 },
  { "Opening door",         LED::LED_ON,           MAXMOVE_DELAY, START_CLOSE, 0 },
  { "Door held open",       LED::LED_ON,         DOOR_OPEN_DELAY, START_CLOSE, 0 },
  { "Start closing door",   LED::LED_ON,           MAXMOVE_DELAY, START_CLOSE, 0 },
  { "Closing door",         LED::LED_ON,           MAXMOVE_DELAY, WAITINGFORCARD, 0 },
};

enum { SILENT, LEAVE, CHECK } buzz, lastbuzz;
unsigned long lastbuzchange = 0;

unsigned long laststatechange = 0, lastReport = 0;
static machinestates_t laststate = OUTOFORDER;
machinestates_t machinestate = BOOTING;

unsigned long opening_door_count  = 0, door_denied_count = 0, swipeouts_count = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  stepper.setMaxSpeed(STEPPER_MAXSPEED);  // divide by 3 to get rpm
  stepper.setAcceleration(STEPPER_ACCELL);
  stepper.moveTo(DOOR_CLOSED);
  stepper.run();
  while (stepper.isRunning()) {
    stepper.run();
  };
  stepper.disableOutputs();

  pinMode(DOOR_SENSOR, INPUT_PULLUP);

  ledcSetup(BUZ_CHANNEL, 2000, 8);
  ledcAttachPin(BUZZER_GPIO, BUZ_CHANNEL);

  node.set_mqtt_prefix("ac");
  node.set_master("master");

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
  node.onApproval([](const char * machine) {
    Debug.println("Got approve");
    if (machinestate < START_OPEN)
      machinestate = START_OPEN;
    opening_door_count++;
  });
  node.onDenied([](const char * machine) {
    Debug.println("Got denied");
    machinestate = REJECTED;
    door_denied_count ++;
  });

  node.onReport([](JsonObject  & report) {
    report["state"] = state[machinestate].label;
    report["opening_door_count"] = opening_door_count;
    report["door_denied_count"] = door_denied_count;
    report["state"] = state[machinestate].label;
    report["opens"] = opening_door_count;
    report["swipeouts"] = swipeouts_count;

#ifdef OTA_PASSWD
    report["ota"] = true;
#else
    report["ota"] = false;
#endif
  });

  reader.onSwipe([](const char * tag) -> ACBase::cmd_result_t {

    // special case of the someone is leaving swipe; technically
    // we sent this as an 'ok to leave' sort of request; so the privacy
    // of the tag/user is preserved on the wire.
    //
    // We expect the door to not be in the opening process; but we do
    // allow a swipe post opening - e.g. when some-one holds the door
    // open als someone comes in.
    //
    if ((machinestate == WAITINGFORCARD || machinestate >= OPEN) && digitalRead(DOOR_SENSOR) == DOOR_IS_OPEN)
    {
      Debug.printf("Detected a leave; sent tag to master.\n");

      node.request_approval(tag, "leave", NULL, false);
      swipeouts_count++;
      buzz = LEAVE;
      return ACBase::CMD_CLAIMED;
    }

    // avoid swithing messing with the door open process
    if (machinestate > CHECKINGCARD) {
      Debug.printf("Ignoring a normal swipe - as we're still in some open process.");
      return ACBase::CMD_CLAIMED;
    }

    // We'r declining so that the core library handle sending
    // an approval request, keep state, and so on.
    //
    Debug.printf("Detected a normal swipe.\n");
    buzz = CHECK;
    return ACBase::CMD_DECLINE;
  });


  // This reports things such as FW version of the card; which can 'wedge' it. So we
  // disable it unless we absolutely positively need that information.
  //
  reader.set_debug(false);
  node.addHandler(&reader);
#ifdef OTA_PASSWD
  node.addHandler(&ota);
#endif

  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));

#if 1
  auto t = std::make_shared<TelnetSerialStream>(telnetSerialStream);
  Log.addPrintStream(t);
  Debug.addPrintStream(t);
#endif

  // node.set_debug(true);
  // node.set_debugAlive(true);
  node.begin();
  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

void buzzer_loop() {
  if (buzz != lastbuzz) {
    switch (buzz) {
      case CHECK:
        ledcSetup(BUZ_CHANNEL, 2000, 8);
        ledcWrite(BUZ_CHANNEL, 127);
        break;
      case LEAVE:
        ledcSetup(BUZ_CHANNEL, 1000, 8);
        ledcWrite(BUZ_CHANNEL, 127);
        break;
      case SILENT:
      default:
        ledcWrite(BUZ_CHANNEL, 0);
        break;
    };
    lastbuzchange = millis();
    lastbuzz = buzz;
  };
  if (millis() - lastbuzchange > 333)
    buzz = SILENT;
}

void loop() {
  node.loop();
  stepper.run();
  buzzer_loop();

  if (laststate != machinestate) {
    Debug.printf("Changed from state <%s> to state <%s>\n",
                 state[laststate].label, state[machinestate].label);

    state[laststate].timeInState += (millis() - laststatechange) / 1000;
    laststate = machinestate;
    laststatechange = millis();
    return;
  }

  if (state[machinestate].maxTimeInMilliSeconds != NEVER &&
      (millis() - laststatechange > state[machinestate].maxTimeInMilliSeconds))
  {
    state[machinestate].timeoutTransitions++;

    laststate = machinestate;
    machinestate = state[machinestate].failStateOnTimeout;

    Log.printf("Time-out; transition from <%s> to <%s>\n",
               state[laststate].label, state[machinestate].label);
    return;
  };

  if (state[machinestate].autoReportCycle && \
      millis() - laststatechange > state[machinestate].autoReportCycle && \
      millis() - lastReport > state[machinestate].autoReportCycle)
  {
    Log.printf("State: %s now for %lu seconds", state[laststate].label, (millis() - laststatechange) / 1000);
    lastReport = millis();
  };

  switch (machinestate) {
    case REBOOT:
      node.delayedReboot();
      break;

    case WAITINGFORCARD:
    case CHECKINGCARD:
    case REJECTED:
      // all handled in above stage engine.
      break;
    case START_OPEN:
      stepper.enableOutputs();
      stepper.moveTo(DOOR_OPEN);
      machinestate = OPENING;
      break;
    case OPENING:
      if (stepper.currentPosition() == DOOR_OPEN) { // no sensors.
        machinestate = OPEN;
      }
      break;
    case OPEN:
      // keep the enableOutputs on - to hold the door. Will timeout.
      break;
    case START_CLOSE:
      stepper.moveTo(DOOR_CLOSED);
      machinestate = CLOSING;
      break;
    case CLOSING:
      if (stepper.currentPosition() == DOOR_CLOSED) {
        machinestate = WAITINGFORCARD;
        stepper.disableOutputs();
      };
      break;

    case BOOTING:
    case OUTOFORDER:
    case TRANSIENTERROR:
    case NOCONN:
      break;
  };
}
