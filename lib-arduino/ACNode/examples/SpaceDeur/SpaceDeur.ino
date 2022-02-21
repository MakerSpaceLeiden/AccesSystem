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
#include <MachineState.h>
#include <RFID.h>   // SPI version

#include <AccelStepper.h>

#ifndef ESP32
#error "The space deur is an ESP32 based Olimex!"
#endif

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
// #define DOOR_SENSOR       (34)
// #define DOOR_IS_OPEN      (LOW)
// #define AARTLED_GPIO      (16) // weggehaald, maart 2019, Lucas

// Introduced by alex - 2020-01-8
#define GROTE_SCHAKELAAR_SENSOR       (34)
#define GROTE_SCHAKELAAR_IS_OPEN      (HIGH)
#define GROTE_SCHAKELAAR_TOPIC        "makerspace/groteschakelaar"

#define BUZZ_TIME (5 * 1000) // Buzz 8 seconds.

ACNode node = ACNode(MACHINE);
RFID reader = RFID();
LED aartLed = LED();    // defaults to the aartLed - otherwise specify a GPIO.

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
#else
#error "You propablly do not want to deploy without OTA"
#endif

MachineState machinestate = MachineState();
// Extra, hardware specific states
MachineState::machinestate_t START_OPEN, OPENING, OPEN, START_CLOSE, CLOSING;


enum { SILENT, LEAVE, CHECK } buzz, lastbuzz;
unsigned long lastbuzchange = 0;

unsigned long laststatechange = 0, lastReport = 0;
// static machinestates_t laststate = OUTOFORDER;

unsigned long opening_door_count  = 0, door_denied_count = 0, swipeouts_count = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  machinestate = MachineState::BOOTING;

  stepper.setMaxSpeed(STEPPER_MAXSPEED);  // divide by 3 to get rpm
  stepper.setAcceleration(STEPPER_ACCELL);
  stepper.moveTo(DOOR_CLOSED);
  stepper.run();
  while (stepper.isRunning()) {
    stepper.run();
  };
  stepper.disableOutputs();

  pinMode(GROTE_SCHAKELAAR_SENSOR, INPUT_PULLUP);

  // setup up the buzzer as a 2000 kHz PWM channel.
  ledcSetup(BUZ_CHANNEL, 2000, 8);
  ledcAttachPin(BUZZER_GPIO, BUZ_CHANNEL);

  // add a sequential set of servo related states; first to set the right angle for open,
  // then wait until it is there; then pause for DOOR_OPEN_DELAY; followed by a close,
  // after which we return to waiting for the cards again.
  //
  CLOSING =  machinestate.addState( "Closing door", LED::LED_ON, MAXMOVE_DELAY, MachineState::WAITINGFORCARD);
  START_CLOSE = machinestate.addState( "Start closing door", LED::LED_ON, MAXMOVE_DELAY, CLOSING);
  OPEN = machinestate.addState( "Door held open", LED::LED_ON, DOOR_OPEN_DELAY, START_CLOSE);
  OPENING = machinestate.addState( "Opening door", LED::LED_ON, MAXMOVE_DELAY, OPEN);
  START_OPEN = machinestate.addState("Start opening door", LED::LED_ON, MAXMOVE_DELAY, OPENING);

  machinestate.setOnChangeCallback(MachineState::ALL_STATES, [](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
    Log.printf("Changing state (%d->%d): %s\n", last, current, machinestate.label());
  });

  node.set_mqtt_prefix("ac");
  node.set_master("master");

  node.onConnect([]() {
    Log.println("Connected");
    machinestate = MachineState::WAITINGFORCARD;
  });
  node.onDisconnect([]() {
    Log.println("Disconnected");
    machinestate = MachineState::NOCONN;

  });
  node.onError([](acnode_error_t err) {
    Log.printf("Error %d\n", err);
    machinestate = MachineState::WAITINGFORCARD;
  });
  node.onApproval([](const char * machine) {
    Debug.println("Got approve");
    if (machinestate.state() < CLOSING)
      machinestate = START_OPEN;
    opening_door_count++;
  });
  node.onDenied([](const char * machine) {
    Debug.println("Got denied");
    machinestate = MachineState::WAITINGFORCARD;
    door_denied_count ++;
  });

  node.onReport([](JsonObject  & report) {
    report["state"] = machinestate.label();

    report["opening_door_count"] = opening_door_count;
    report["door_denied_count"] = door_denied_count;

    report["opens"] = opening_door_count;
    report["swipeouts"] = swipeouts_count;

#ifdef OTA_PASSWD
    report["ota"] = true;
#else
    report["ota"] = false;
#endif
  });

  reader.onSwipe([](const char * tag) -> ACBase::cmd_result_t {
    Log.println("Swipe");

    if (machinestate.state() > MachineState::CHECKINGCARD) {
      Debug.printf("Ignoring a normal swipe - as we're still in some open process: %d/%s.",
      machinestate.state(), machinestate.label());
      return ACBase::CMD_CLAIMED;
    }

    // We'r declining so that the core library handle sending
    // an approval request, keep state, and so on.
    //
    Log.printf("Detected a normal swipe.\n");
    buzz = CHECK;

    return ACBase::CMD_DECLINE;
  });

  node.addHandler(&reader);
#ifdef OTA_PASSWD
  node.addHandler(&ota);
#endif


  node.addHandler(&machinestate);
  // This reports things such as FW version of the card; which can 'wedge' it. So we
  // disable it unless we absolutely positively need that information.
  //
  reader.set_debug(false);

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

void grote_schakelaar_loop() {
  // debounce
  static unsigned long lst = 0;
  static int last_grote_schakelaar = digitalRead(GROTE_SCHAKELAAR_SENSOR);

  if (digitalRead(GROTE_SCHAKELAAR_SENSOR) != last_grote_schakelaar && lst == 0) {
    last_grote_schakelaar = digitalRead(GROTE_SCHAKELAAR_SENSOR);
    lst = millis();
  };

  // Start trusting the value once it has been stable for 100 milli Seconds.
  //
  if (lst && millis() - lst > 100) {
    // stable for over 100 milliseconds; so we trust this value;
    if (last_grote_schakelaar == GROTE_SCHAKELAAR_IS_OPEN) {
      Log.println("Grote schakelaar: Space is now open.");
      node.send(GROTE_SCHAKELAAR_TOPIC, "1");
    } else {
      Log.println("Grote schakelaar: Space is now closed.");
      node.send(GROTE_SCHAKELAAR_TOPIC, "0");
    };
    lst = 0;
  }
}

void loop() {
  node.loop();
  stepper.run();
  buzzer_loop();
  grote_schakelaar_loop();

#if 0
  static unsigned long t = 0;
  if (millis() - t > 5000) {
    t = millis();
    Log.printf("state: %d \n", machinestate.state());
  };
#endif

  if (machinestate.state() == START_OPEN) {
    stepper.enableOutputs();
    stepper.moveTo(DOOR_OPEN); // set end poistion.
    machinestate = OPENING;
  }
  else if (machinestate.state() == OPENING) {
    if (stepper.currentPosition() == DOOR_OPEN) { // no sensors - so wait until we hit the end position
      machinestate = OPEN;
    }
  }
  else if (machinestate.state() ==  OPEN) {
    // just wait - automatic timeout in the state engine.
  }
  else if (machinestate.state() == START_CLOSE) {
    stepper.moveTo(DOOR_CLOSED);
    machinestate = CLOSING;
  }
  else if (machinestate.state() == CLOSING) {
    if (stepper.currentPosition() == DOOR_CLOSED) {
      machinestate = MachineState::WAITINGFORCARD;
      stepper.disableOutputs();
    };
  } else {
    switch (machinestate.state()) {
      case MachineState::REBOOT:
        node.delayedReboot();
        break;
    };
  };

}
