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

  Main Settings for White boards and later (EPS32, POE)

  Board:             "ESP32-WROOM-DA Module"
  Core Debug Level:  "None"
  Flash Size:        "4MB (32Mb)"
  Partition Scheme:  "Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)"
  Upload Speed:      "921600"

  As deployed May 2025:
  https://wiki.makerspaceleiden.nl/mediawiki/index.php/PowerNodeBlue-bringup
  https://wiki.makerspaceleiden.nl/mediawiki/index.php/Node_Spacedeur

  Mechanics

  1)  Stepper motor with a pully; rope to the door handle
  2)  https://wiki.makerspaceleiden.nl/mediawiki/index.php/Project_Grote_Schakelaar

  Note: Espressif ESP32 3.2.0 seems to have an older version of MBED-TLS than
      the 2.0.15 and later versions (with mbedtls_sha256_starts_ret not yet
      replacing the legacy mbedtls_sha256_starts()).

*/
#include <BlackNodev111.h>

#include <AccelStepper.h>  // for the stepper motor.
#include "esp_task_wdt.h"

#define MACHINE "spacedeur"
BlackNodev111 node = BlackNodev111(MACHINE);

#ifndef OTA_PASSWD_HASH
// Comment out this line if you are experimenting, etc.
#error "You propablly do not want to deploy without OTA"
#endif

// Extra, hardware specific states for the stepper motor.
//
MachineState::machinestate_t START_OPENING_DOOR, OPENING_DOOR, OPEN_DOOR, START_CLOSING_DOOR, CLOSING_DOOR;

// Max time to let the stepper move with the stepper
// after this we give up & move to the next state.
//
#define MAXMOVE_DELAY (30 * 1000)

// Number of steps/position for either end.
//
// No end stopper; assumed to be closed at startup (spring
// in doorhandle will generally unspool the rope if the
// motor is not held by a stop current/short-circuit).
//
long DOOR_CLOSED = 0;
long DOOR_ROPELEN = 300;

#define DOOR_SENSE_OPEN (node.IOE)
// #define DOOR_SENSE_CLOSED (node.IOD)
#define DOOR_SENSOR(x) ((expandedDigitalRead((x)) == LOW) ? true : false)

// How long to keep the door open
#define DOOR_OPEN_DELAY (10 * 1000)


// See https://mailman.makerspaceleiden.nl/mailman/private/deelnemers/2019-February/019837.html
//
// #define DOOR_SENSOR       (34)
// #define DOOR_IS_OPEN      (LOW)
// #define AARTLED_GPIO      (16) // weggehaald, maart 2019, Lucas
// Introduced by alex - 2020-01-8
//
#define GROTE_SCHAKELAAR_SENSOR (node.IOA)  // Was 34
#define GROTE_SCHAKELAAR_IS_OPEN (HIGH)
#define GROTE_SCHAKELAAR_TOPIC "makerspace/groteschakelaar"

void setup_grote_schakelaar() {
  expandedPinMode(GROTE_SCHAKELAAR_SENSOR, INPUT);
}

void grote_schakelaar_loop() {
  // debounce
  static unsigned long lst = 0;
  static int last_grote_schakelaar = expandedDigitalRead(GROTE_SCHAKELAAR_SENSOR);

  if (expandedDigitalRead(GROTE_SCHAKELAAR_SENSOR) != last_grote_schakelaar) {
    last_grote_schakelaar = expandedDigitalRead(GROTE_SCHAKELAAR_SENSOR);
    lst = millis();
  };

  // Start trusting the value once it has been stable for 100 milli Seconds.
  //
  if (lst && millis() - lst > 100) {
    // stable for over 100 milliseconds; so we trust this value;
    if (last_grote_schakelaar == GROTE_SCHAKELAAR_IS_OPEN) {
      Log.println("Grote schakelaar: Space is now open.");
      // node.send(GROTE_SCHAKELAAR_TOPIC, "1");
    } else {
      Log.println("Grote schakelaar: Space is now closed.");
      // node.send(GROTE_SCHAKELAAR_TOPIC, "0");
    };
    lst = 0;
  }
}

// Stepper motor-Pololu / A4988 - wiring
//
#define STEPPER_DIR (0)     // was 2
#define STEPPER_SLEEP (12)  // was 4
#define STEPPER_STEP (2)    // was 5

#define STEPPER_MAXSPEED (7000)  // was (1850)
#define STEPPER_ACCELL (2500)    // was (850)

// Simple overlay of the AccelStepper that configures for the A4988
// driver of a 4 wire stepper-including the additional enable wire.
// and makes sure it comes on in the 'off' position. So we do not
// get a loud 'click' on startup.
//
class PololuStepper : public AccelStepper {
public:
  PololuStepper(uint8_t step_pin = 0xFF, uint8_t dir_pin = 0xFF, uint8_t sleep_pin = 0xFF)
    : AccelStepper(AccelStepper::DRIVER, step_pin, dir_pin) {

    pinMode(STEPPER_SLEEP, OUTPUT);
    digitalWrite(STEPPER_SLEEP, LOW);  // dis-able stepper first.
    setPinsInverted(false, false, false);
    setEnablePin(sleep_pin);

    setMaxSpeed(STEPPER_MAXSPEED);
    setAcceleration(STEPPER_ACCELL);

    // power it down - to prevent the stepper motor from
    // needlessly heating up (in the door closed position
    // the motor does not need to actively 'brake').
    //
    disableOutputs();
  }
};

PololuStepper stepper = PololuStepper(STEPPER_STEP, STEPPER_DIR, STEPPER_SLEEP);

unsigned long laststatechange = 0, lastReport = 0;
unsigned long opening_door_count = 0, door_denied_count = 0, opens = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__);

  // Disable - as we need this pin for the stepper motor.
  //
  node.BUTT2 = -1;


  // add a sequential set of servo related states; first to set the right angle for open,
  // then wait until it is there; then pause for DOOR_OPEN_DELAY; followed by a close,
  // after which we return to waiting for the cards again.
  //
  // We set a timeout on each; but will change state quicker (see the switch() statment
  // in loop() below) when the right number of steps (DOOR_OPEN, DOOR_CLOSE) is reached.
  //
  CLOSING_DOOR = node.machinestate.addState("Closing door", LED::LED_ON, MAXMOVE_DELAY, MachineState::WAITINGFORCARD);
  START_CLOSING_DOOR = node.machinestate.addState("Start closing door", LED::LED_ON, MAXMOVE_DELAY, CLOSING_DOOR);
  OPEN_DOOR = node.machinestate.addState("Door held open", LED::LED_ON, DOOR_OPEN_DELAY, START_CLOSING_DOOR);
  OPENING_DOOR = node.machinestate.addState("Opening door", LED::LED_ON, MAXMOVE_DELAY, OPEN_DOOR);
  START_OPENING_DOOR = node.machinestate.addState("Start opening door", LED::LED_ON, MAXMOVE_DELAY, OPENING_DOOR);

  // Change to something like debug or test
  // if you want to send all output to a different
  // set of MQTT channels.
  //
  // node.set_mqtt_prefix("test");

  node.onApproval([](const char* machine) {
    if (node.machinestate.state() < START_OPENING_DOOR) {
      node.machinestate = START_OPENING_DOOR;
      opens++;  // i.e. only count if we actually opened the door.
    };
    opening_door_count++;
  });
  node.onDenied([](const char* machine) {
    door_denied_count++;
  });

  node.onReport([](JsonObject& report) {
    report["state"] = node.machinestate.label();

    report["opening_door_count"] = opening_door_count;
    report["door_denied_count"] = door_denied_count;

    report["opens"] = opens;

    // attempt to track down MQTT issue.
    //
    report["mqtt_host"] = String(node.mqtt_server);
    report["mqtt_port"] = String(node.mqtt_port);
    report["mqtt_isUp"] = node.isUp();
    report["mqtt_isConnected"] = node.isConnected();
  });

#ifdef OTA_PASSWD_HASH
  node.setOTAPasswordHash(ota_password_hash);
#endif

  // node.set_debug(true);
  // node.set_debugAlive(true);
  node.begin();


#ifdef DOOR_SENSE_OPEN
  expandedPinMode(DOOR_SENSE_OPEN, INPUT);
#endif

#ifdef DOOR_SENSE_CLOSED
  expandedPinMode(DOOR_SENSE_CLOSED, INPUT);
#endif

  expandedPinMode(node.IOA, INPUT);
  expandedPinMode(node.IOB, INPUT);
  expandedPinMode(node.IOC, INPUT);
  expandedPinMode(node.IOD, INPUT);
  expandedPinMode(node.IOE, INPUT);

  setup_grote_schakelaar();

  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__);
  // esp_task_wdt_deinit();
  esp_task_wdt_init(60 * 1000, false);
}

void loop() {
#if 1
  static unsigned long lst = millis() - 5000;
  if (millis() - lst > 1000) {
    lst = millis();
#if 0
    Debug.printf("Open: %d, Close %d - %d/%d\n",
                 DOOR_SENSOR(DOOR_SENSE_OPEN), DOOR_SENSOR(DOOR_SENSE_CLOSED),
                 expandedDigitalRead(DOOR_SENSE_OPEN), expandedDigitalRead(DOOR_SENSE_CLOSED));
#endif
#if 0
    Debug.printf("A: %x=%d, B: %x=%d, C: %x=%d, D: %x=%d, E: %x=%d\n",
                 node.IOA, expandedDigitalRead(node.IOA),
                 node.IOB, expandedDigitalRead(node.IOB),
                 node.IOC, expandedDigitalRead(node.IOC),
                 node.IOD, expandedDigitalRead(node.IOD),
                 node.IOE, expandedDigitalRead(node.IOE)
                );
#endif
  };
#endif

  node.loop();
  grote_schakelaar_loop();
  stepper.run();

  if (node.machinestate == START_OPENING_DOOR) {
    stepper.enableOutputs();

    DOOR_CLOSED = stepper.currentPosition();
    stepper.moveTo(DOOR_CLOSED + DOOR_ROPELEN);  // specify end position.
    Log.printf("Move open by %ld\n", DOOR_ROPELEN);
    node.machinestate = OPENING_DOOR;
  } else if (node.machinestate == OPENING_DOOR) {
#ifdef DOOR_SENSE_OPEN
    if (stepper.currentPosition() >= DOOR_CLOSED + DOOR_ROPELEN && !DOOR_SENSOR(DOOR_SENSE_OPEN)) {
      DOOR_ROPELEN += 5;
      stepper.moveTo(DOOR_CLOSED + DOOR_ROPELEN);  // specify end position.
    };
    if (DOOR_SENSOR(DOOR_SENSE_OPEN)) {
      stepper.stop();
      DOOR_ROPELEN = stepper.currentPosition() - DOOR_CLOSED - 5;
      Log.printf("Adjust rope len to %ld\n", DOOR_ROPELEN);
    };
#else
    // no sensors - so wait until we hit the end position
    // by stepper count
#endif
    if (stepper.currentPosition() >= DOOR_CLOSED + DOOR_ROPELEN) {
      node.machinestate = OPEN_DOOR;
    };
    // we do not disable the current - as to get a `hold' action of the
    // stepper against the tension of the spring in the door handle.
  } else if (node.machinestate == START_CLOSING_DOOR) {
    stepper.moveTo(DOOR_CLOSED);
    Log.printf("Move close to %ld\n", DOOR_CLOSED);
    node.machinestate = CLOSING_DOOR;
  } else if (node.machinestate == CLOSING_DOOR) {
#ifdef DOOR_SENSE_CLOSED
    if (stepper.currentPosition() <= DOOR_CLOSED && !DOOR_SENSOR(DOOR_SENSE_CLOSED)) {
      DOOR_CLOSED -= 10;
      stepper.moveTo(DOOR_CLOSED);
    };
    if (DOOR_SENSOR(DOOR_SENSE_CLOSED)) {
      stepper.stop();
      DOOR_CLOSED = stepper.currentPosition();
      Log.printf("Adjust close to %ld\n", DOOR_CLOSED);
    };
#else
    // no sensors - so wait until we hit the end position
    // by stepper count
#endif

    if (stepper.currentPosition() <= DOOR_CLOSED) {
      // We disable power as to not `hold' the stepper & get
      // the motor needlessly hot.
      //
      stepper.disableOutputs();

      // and go back to wiating for card.
      node.machinestate = MachineState::WAITINGFORCARD;
    };
  };
};
