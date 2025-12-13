/*
      Copyright 2015-2025 Dirk-Willem van Gulik <dirkx@webweaving.org>
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

  Board: v1.14 / Blue; without screen; extra connector for the
       inside door lock/open buttons.

  Compile settings:
  - ESP32-WROOM-DA Module (or ESP32 Dev) with minial SPIFFs
  Wiring:
  - https://wiki.makerspaceleiden.nl/mediawiki/index.php/Node_Olga_Binnen
  QR code:
  - https://wiki.makerspaceleiden.nl/mediawiki/index.php/QR_olgabinnen

Lock states

  NIGHT_LOCK      from 18:00 to 08:00 or as requested by a (long) red button press
  DAY_LOCK        Monday to saturday from first opening after 08:00 until 18:00
                  Or for 1 hours after any opening outside 08:00 and 18:00
  UNLOCKED        From the moment requested by a long green button press
                  terminated by time or by a short red button press.
*/

enum { NIGHT_LOCK,  // lock in night mode; solenoid cannot open the door
       DAY_LOCK,    // lock in day mode, solenoid off, engaging it opens the door
       UNLOCKED     // lock in day mode, solenoid engaged
} doorstate;

#include <BlueNodev114.h>

#ifndef ARDUINO_PARTITION_min_spiffs 
#error "Unexpected partition table; may break OTA"
#endif
#ifndef ARDUINO_ESP32_WROOM_DA
#error "Black/Blue Hardware is expected to be an ESP32 WROOM-DA"
#endif

#define MACHINE "olgabuiten"

#define DAY_OPEN (node.OUT0)
#define DAY_SOLENOID (node.OUT1)

#define DOOR_UNLOCK_ALERT (node.OPTO0)
#define DOOR_OPEN_ALERT (node.OPTO1)

#define LED_BUTTON_RED (node.IOE)
#define BUTTON_RED (node.IOD)  // pull down to ground, active high

#define LED_BUTTON_GREEN (node.IOC)
#define BUTTON_GREEN (node.IOB)  // pull down to ground, active high

#define BUZZ_TIME (0.5)  // Pulse to the lock & solenoid
#define BUZZ2_TIME (5)   // How long we hold the solenoid.

// Generate with 'echo -n Password | openssl sha256 or
// use https://emn178.github.io/online-tools/sha256.html.

// Note: No \0, cariage return or linefeed  at the end of the
//       password; just the characters of the password. The
//       String 'Password' should yield e7cf....221a.
//
#ifndef OTA_PASSWD_HASH256
#error "An OTA password hash(sha256) MUST be set. Sorry."
#endif

const char ota_password_hash[] = OTA_PASSWD_HASH256;
BlueNodev114 node = BlueNodev114(MACHINE, WIFI_NETWORK, WIFI_PASSWD);

// Extra, hardware specific states
MachineState::machinestate_t BUZZING;  // short signal to lock
MachineState::machinestate_t BRACKET;  // signal to lock sent - engaging solenoid

unsigned long opening_door_count = 0, door_denied_count = 0, key_open_count = 0, button_open_count = 0, unlock_count = 0, pass_count = 0;

IODebounce *doorOpenDetect, *doorUnlockDetect, *redButtonDetect, *greenButtonDetect;

// we're not yet including a holiday schedule or anyting like that yet.
// will add some rest API to the CRM for this at some point.
//
// Right now - working days are defined as
// monday to saturday 08:00 to 18:00 Local time.
//
bool isWorkingHours() {
  const time_t now = time(NULL);
  if (now < 1765000000)
    return false;  // we have not yet synced with NTP
  struct tm *ts = localtime(&now);
  if (ts->tm_hour < 8 || ts->tm_hour > 18)
    return false;
  if (ts->tm_wday == 0 /* sunday */)
    return false;
  return true;
};

void setup() {
  Serial.println("setup(): " __FILE__ " " __DATE__ " " __TIME__);

  digitalWrite(DAY_OPEN, LOW);
  pinMode(DAY_OPEN, OUTPUT);
  node.setMonitoredOutput(DAY_OPEN, LOW);

  digitalWrite(DAY_SOLENOID, LOW);
  pinMode(DAY_SOLENOID, OUTPUT);
  node.setMonitoredOutput(DAY_SOLENOID, LOW);

  // lock is in day setting - keep the solenoid engaged.
  BRACKET = node.machinestate.addState((const char *)"Stil; buzzing",
                                       LED::LED_IDLE,
                                       (time_t)(BUZZ2_TIME * 1000),       // stay in this state for BUZZ_TIME seconds
                                       node.machinestate.WAITINGFORCARD,  // then go back to waiting for the next swipe.
                                       false /* no OTA during this */,
                                       false /* No reporting until we're done with the door. */
  );
  // pulse the lock to day during this time
  BUZZING = node.machinestate.addState((const char *)"Buzzing",
                                       LED::LED_IDLE,
                                       (time_t)(BUZZ_TIME * 1000),  // stay in this state for BUZZ_TIME seconds
                                       BRACKET,                     // then go to idle for a bit.
                                       false /* no OTA during this */,
                                       false /* No reporting until we're done with the door. */
  );

  expandedPinMode(LED_BUTTON_RED, AW9523_LED_MODE);
  expandedAnalogWrite(LED_BUTTON_RED, 0);

  expandedPinMode(LED_BUTTON_GREEN, AW9523_LED_MODE);
  expandedAnalogWrite(LED_BUTTON_GREEN, 0);

  expandedPinMode(DOOR_UNLOCK_ALERT, INPUT);
  doorUnlockDetect = new IODebounce("DoorUnlockAlert", DOOR_UNLOCK_ALERT);
  doorUnlockDetect->setDigitalReadFunction(&expandedDigitalRead);
  doorUnlockDetect->setCallback([](const int newState) {
    Log.println(newState ? "Door reported unlocked" : "Door reported locked");
    if ((newState) && (doorstate != NIGHT_LOCK))
      Log.println("**** assumption error *** reported locked but not in night state ?!?! ****. Dear humans - investigate !");
  });
  node.addHandler(doorUnlockDetect);

  expandedPinMode(DOOR_OPEN_ALERT, INPUT);
  doorOpenDetect = new IODebounce("DoorOpenAlert", DOOR_OPEN_ALERT);
  doorOpenDetect->setDigitalReadFunction(&expandedDigitalRead);
  doorOpenDetect->setCallback([](const int newState) {
    if (newState) {
      if (doorstate == NIGHT_LOCK && isWorkingHours()) {
        Log.println("Door was closed and swithing to day state");
        doorstate = DAY_LOCK;
        return;
      };
      Log.println("Door was closed");
      return;
    };
  node.addHandler(doorOpenDetect);

    if (node.machinestate != MachineState::CHECKINGCARD) {
      Debug.println("Ignoring door open alert - we triggered it.");
      return;
    };
    key_open_count++;
    Log.println("Door openened with a key");
  });

  expandedPinMode(BUTTON_RED, INPUT);
  redButtonDetect = new IODebounce("RedButton", BUTTON_RED);
  redButtonDetect->setDigitalReadFunction(&expandedDigitalRead);
  redButtonDetect->setCallback([](const int newState) {
    // Button is active high
    if (newState == 0)
      return;
    if (doorstate != NIGHT_LOCK) {
      Debug.println("Press of red button ignored - lock already in night setting");
      return;
    };
    Log.println("Press of red button, night lock after next close");
    node.machinestate = BUZZING;  // as you propably want to exit too.
    doorstate = NIGHT_LOCK;
  });
  node.addHandler(redButtonDetect);

  expandedPinMode(BUTTON_GREEN, INPUT);
  greenButtonDetect = new IODebounce("GreenButton", BUTTON_GREEN);
  greenButtonDetect->setDigitalReadFunction(&expandedDigitalRead);
  greenButtonDetect->setCallback([](const int newState) {
    static unsigned long lastPress = 0;
    // Button is active high
    if (newState) {
      lastPress = millis();
      node.machinestate = BUZZING;
      Log.println("Opening door on green button press");
      button_open_count++;
      return;
    };
    if (millis() - lastPress > 2000) {
        doorstate = UNLOCKED;
        Log.println("Long press on green - activating pass-mode");
        pass_count++;
        return;
    };
    unlock_count++;
    Debug.println("Ignoring release of the green button - too short to trigger pass-mode");
  });
  node.addHandler(greenButtonDetect);

  node.onApproval([](const char *machine) {
    Log.printf("Engaging the solenoid/buzzer\n");
    node.machinestate = (doorstate == UNLOCKED) ? BRACKET : BUZZING;
    opening_door_count++;
  });

  node.onDenied([](const char *machine) {
    node.buzzerErr();
    door_denied_count++;
  });

  node.setOTAPasswordHash(ota_password_hash);

  node.onReport([](JsonObject &report) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s %s %s", FILE2FIRMWARE(__FILE__), __DATE__, __TIME__);
    report["fw"] = tmp;
    report["count_open"] = opening_door_count;
    report["count_key_open"] = key_open_count;
    report["count_denied"] = door_denied_count;
    report["count_button_open"] = button_open_count;
    report["count_button_unlock"] = unlock_count;
    report["count_button_to_passstate"] = pass_count;
  });

  node.begin(false /* no OLED screen */);

  Log.printf("Booted: %s " __DATE__ " " __TIME__, FILE2FIRMWARE(__FILE__));
}

void loop() {
  bool in_open = node.machinestate.state() == BUZZING || node.machinestate.state() == BRACKET;

  node.setMonitoredOutput(DAY_OPEN, doorstate != NIGHT_LOCK || node.machinestate.state() == BUZZING);
  node.setMonitoredOutput(DAY_SOLENOID, in_open || doorstate == UNLOCKED);
  node.buzzer(in_open);

  // light the LED when it makes sense to press them. We may need to do the 
  // exact opposite - i.e. let the LED not reflect the action you can do
  // with the button - but the state that the button brought the lock into.
  //
  expandedAnalogWrite(LED_BUTTON_GREEN, (doorstate != UNLOCKED) ? 0 : 255);
  expandedAnalogWrite(LED_BUTTON_RED, (doorstate != NIGHT_LOCK) ? 0 : 255);

  if (doorstate != NIGHT_LOCK && !isWorkingHours() && node.machinestate == MachineState::CHECKINGCARD && node.machinestate.secondsInThisState() > 300) {
    Log.println("Detecting end of the working day - switching to night lock ");
    doorstate = NIGHT_LOCK;
  };
  if (doorstate == DAY_LOCK && !isWorkingHours() && node.machinestate.secondsInThisState() > 3600) {
    Log.println("Not seen anyone for over an hour; going to night lock as it is outside working hours");
    doorstate = NIGHT_LOCK;
  };

  node.loop();
}
