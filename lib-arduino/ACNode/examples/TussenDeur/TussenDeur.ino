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

#define MACHINE          "tussendeur"

#define SOLENOID_GPIO     (4)
#define SOLENOID_OFF      (LOW)
#define SOLENOID_ENGAGED  (HIGH)

#define AARTLED_GPIO      (16)

#define BUZZ_TIME (8) // Buzz 8 seconds.

ACNode node = ACNode(MACHINE);
RFID reader = RFID();
LED aartLed = LED();    // defaults to the aartLed - otherwise specify a GPIO.

#ifndef OTA_PASSWD
#error "Are you sure you want this ?! as it will disable OTA programming"
#else
OTA ota = OTA(OTA_PASSWD);
#endif

MachineState machinestate = MachineState();
// Extra, hardware specific states
MachineState::machinestate_t BUZZING;

unsigned long opening_door_count  = 0, door_denied_count = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  // Init the hardware and get it into a safe state.
  //
  pinMode(SOLENOID_GPIO, OUTPUT);
  digitalWrite(SOLENOID_GPIO, SOLENOID_OFF);

  // Add the states needed for this node.
  //
  BUZZING = machinestate.addState((const char*)"Approved",
                                  LED::LED_IDLE,
                                  (time_t)(BUZZ_TIME * 1000), // stay in this state for BUZZ_TIME seconds
                                  machinestate.WAITINGFORCARD // then go back to waiting for the next swipe.
                                 );

  machinestate.setOnChangeCallback(MachineState::ALL_STATES, [](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
    Log.printf("Changing state (%d->%d): %s\n", last, current, machinestate.label());
    aartLed.set(machinestate.ledState());
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
    machinestate = BUZZING;
    opening_door_count++;
  });
  node.onDenied([](const char * machine) {
    machinestate = MachineState::WAITINGFORCARD;
    door_denied_count ++;
  });
  node.onReport([](JsonObject  & report) {
    report["state"] = machinestate.label();
    report["opening_door_count"] = opening_door_count;
    report["door_denied_count"] = door_denied_count;
    report["opens"] = opening_door_count;
#ifdef OTA_PASSWD
    report["ota"] = true;
#else
    report["ota"] = false;
#endif
  });


  node.addHandler(&reader);
#ifdef OTA_PASSWD
  node.addHandler(&ota);
#endif

  // This reports things such as FW version of the card; which can 'wedge' it. So we
  // disable it unless we absolutely positively need that information.
  //
  reader.set_debug(false);
  
  // Enabling these will cause privacy sensitive information to appear
  // in the logs - so best only used during development.
  //
  // node.set_debug(true);
  // node.set_debugAlive(true);
  node.addHandler(&machinestate);

  node.begin();
  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

void loop() {
  node.loop();

  // handle the open functon 'always'. Which boils down to
  // turing the MOSFET that controils the solenoid of the lock
  // on when we are in buzzing mode. Buzzing mode has a timeout
  // of BUZZ_TIME - after which we return back to WAITINGFORCARD.
  //
  digitalWrite(SOLENOID_GPIO, (machinestate.state() == BUZZING) ? SOLENOID_ENGAGED : SOLENOID_OFF);

  // Allmost all state is handled automatic with the defaults; except for
  // the one were we get in such a weird one that we need to reboot in a
  // last ditch attempt.
  //
  switch (machinestate.state()) {
    case MachineState::REBOOT:
      node.delayedReboot();
      break;
  };
}
