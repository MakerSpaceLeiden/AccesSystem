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

// It is possible to for the door into lock mode during the day; until
// a long press on green disables that.
//
bool forced_night = false;

typedef enum { NIGHT_LOCK = 0,  // lock in night mode; solenoid cannot open the door
               DAY_LOCK,        // lock in day mode, solenoid off, engaging it opens the door
               UNLOCKED         // lock in day mode, solenoid engaged
} doorstate_t;
doorstate_t doorstate = NIGHT_LOCK, last_doorstate = UNLOCKED;

const char *doorstate_label[] = {
  "nightlatch",
  "daylatch",
  "passage",
};

#include <BlueNodev114.h>
#include <util/cufflink_heartbeat.h>

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

#define LED_PASSAGE_MODE (node.IOA)  // not yet wired up.

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

unsigned long opening_door_count = 0, door_denied_count = 0, key_open_count = 0, button_open_count = 0, unlock_count = 0, pass_count = 0, alert_count = 0;

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

  struct tm ts;
  if (!getLocalTime(&ts)) {
    Log.println("Failed to obtain local time");
    return false;
  }
  // Serial.println(&ts, "C %A, %B %d %Y %H:%M:%S zone %Z %z ");

  if (ts.tm_hour < 8 || ts.tm_hour > 17 || (ts.tm_hour > 16 && ts.tm_min > 30))
    return false;       // 9 am to 16:30 local time
  if (ts.tm_wday == 6)  // Saturday
    return false;
  if (ts.tm_wday == 0 /* Sunday */)
    return false;
  return true;
};

bool isDaytime() {
  const time_t now = time(NULL);
  if (now < 1765000000)
    return false;  // we have not yet synced with NTP

  struct tm ts;
  if (!getLocalTime(&ts)) {
    Log.println("Failed to obtain local time");
    return false;
  }
  // Serial.println(&ts, "C %A, %B %d %Y %H:%M:%S zone %Z %z ");

  if (ts.tm_hour < 8 || ts.tm_hour > 18 || (ts.tm_hour > 17 && ts.tm_min > 30))
    return false;  // outside 9 am to 17:30 local time

  // saturday
  if (ts.tm_wday == 6 && (ts.tm_hour < 10 || ts.tm_hour > 15))
    return false;  // Saturday - before 10 or after 15:00

  /* sunday */
  if (ts.tm_wday == 0 && (ts.tm_hour < 12 || ts.tm_hour > 15))
    return false;  // Sunday - before 12 or after 15:00

  return true;
}

void setup() {
  Serial.println("setup(): " __FILE__ " " __DATE__ " " __TIME__);

  digitalWrite(DAY_OPEN, LOW);
  pinMode(DAY_OPEN, OUTPUT);
  node.setMonitoredOutput(DAY_OPEN, LOW);

  digitalWrite(DAY_SOLENOID, LOW);
  pinMode(DAY_SOLENOID, OUTPUT);
  node.setMonitoredOutput(DAY_SOLENOID, LOW);

  // Call this early - we need the extended GPIO set up.
  //
  node.setOTAPasswordHash(ota_password_hash);
  node.begin(false /* no OLED screen */);

  expandedPinMode(LED_BUTTON_RED, AW9523_LED_MODE);
  expandedAnalogWrite(LED_BUTTON_RED, 255);

  expandedPinMode(LED_BUTTON_GREEN, AW9523_LED_MODE);
  expandedAnalogWrite(LED_BUTTON_GREEN, 255);

  expandedPinMode(LED_PASSAGE_MODE, AW9523_LED_MODE);
  expandedAnalogWrite(LED_PASSAGE_MODE, 255);

  // latch is in day setting - keep the solenoid engaged.
  BRACKET = node.machinestate.addState((const char *)"Still buzzing",
                                       LED::LED_IDLE,
                                       (time_t)(BUZZ2_TIME * 1000),       // stay in this state for BUZZ_TIME seconds
                                       node.machinestate.WAITINGFORCARD,  // then go back to waiting for the next swipe.
                                       false /* no OTA during this */,
                                       false /* No reporting until we're done with the door. */
  );
  // pulse the latch into day modus during this time
  BUZZING = node.machinestate.addState((const char *)"Buzzing",
                                       LED::LED_IDLE,
                                       (time_t)(BUZZ_TIME * 1000),  // stay in this state for BUZZ_TIME seconds
                                       BRACKET,                     // then go to idle for a bit.
                                       false /* no OTA during this */,
                                       false /* No reporting until we're done with the door. */
  );

  expandedPinMode(DOOR_UNLOCK_ALERT, INPUT);
  doorUnlockDetect = new IODebounce("DoorUnlockAlert", DOOR_UNLOCK_ALERT);
  doorUnlockDetect->setDigitalReadFunction(&expandedDigitalRead);
  doorUnlockDetect->setCallback([](const int newState) {
    Log.println(newState ? "Door reported unlocked" : "Door reported locked");

    if (!newState) {
      if (!forced_night && doorstate == NIGHT_LOCK && isWorkingHours()) {
        Log.println("Door was closed and swithing to day state as it is within working hours");
        doorstate = DAY_LOCK;
        return;
      };
      Log.printf("Door was closed; and %s workinghours.\n",
                 forced_night ? "it is kept closed after a red button call, even though it is inside" : "it is outside");
      return;
    };

    if ((!newState) && (doorstate != NIGHT_LOCK)) {
      Log.println("**** assumption error *** reported locked but not in night state ?!?! ****. Dear humans - investigate !");
      alert_count++;
    };
  });
  node.addHandler(doorUnlockDetect);

  // Check if we need to go to day/night after we completed a door operning cycle.
  //
  node.machinestate.addOnChangeCallback(BUZZING, [](MachineState::machinestate_t oldState, MachineState::machinestate_t newState) {
    // check during the buzzing if we need to leave the in day state post our opening.
    //
    if ((newState == BUZZING) && !forced_night && isWorkingHours() && (doorstate == NIGHT_LOCK)) {
      Log.println("Switching to day state as it is within working hours");
      doorstate = DAY_LOCK;
    };
  });

  expandedPinMode(DOOR_OPEN_ALERT, INPUT);
  doorOpenDetect = new IODebounce("DoorOpenAlert", DOOR_OPEN_ALERT);
  doorOpenDetect->setDigitalReadFunction(&expandedDigitalRead);
  doorOpenDetect->setCallback([](const int newState) {
    if (!newState)
      return;
    if (node.machinestate != MachineState::WAITINGFORCARD) {
      Debug.println("Ignoring door open alert - we triggered it.");
      return;
    };
    key_open_count++;
    Log.println("Door openened with a key");
  });
  node.addHandler(doorOpenDetect);

  expandedPinMode(BUTTON_RED, INPUT);
  redButtonDetect = new IODebounce("RedButton", BUTTON_RED);
  redButtonDetect->setDigitalReadFunction(&expandedDigitalRead);
  redButtonDetect->setCallback([](const int newState) {
    // Button is active high
    if (newState == 0)
      return;

    switch (doorstate) {
      case NIGHT_LOCK:
        Debug.println("Press of red button ignored - lock already in night setting");
        return;
        break;
      case DAY_LOCK:
        if (forced_night) {
          forced_night = false;
          Log.println("Press of red button - disabling forced night lock & back to day lock");
        } else {
          forced_night = true;
          Log.println("Press of red button furing the day - ending day & enabling the night lock");
          doorstate = NIGHT_LOCK;
        }
        break;
      case UNLOCKED:
        Log.println("Press of red button - disabling passage");
        doorstate = isWorkingHours() ? DAY_LOCK : NIGHT_LOCK;
        break;
    };
    node.machinestate = BUZZING;  // as you propably want to exit too.
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
      // Pressing the red button will `force' the door closed, even during
      // working hours until either the end of the working day or until the
      // Green button is pressed long.
      if (forced_night) {
        Log.println("Ending forced night");
        forced_night = false;
      };
      return;
    };
    unlock_count++;
    Debug.println("Ignoring release of the green button - too short to trigger pass-mode");
  });
  node.addHandler(greenButtonDetect);

  node.onApproval([](const char *machine) {
    Log.printf("Engaging the solenoid/buzzer for %s\n", node.lastApproved()->name);

    node.machinestate = (doorstate == UNLOCKED) ? BRACKET : BUZZING;
    opening_door_count++;
  });

  node.onDenied([](const char *machine) {
    node.buzzerErr();
    door_denied_count++;
  });


  node.onReport([](JsonObject report) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s %s %s", FILE2FIRMWARE(__FILE__), __DATE__, __TIME__);
    report["fw"] = tmp;

    JsonObject stats = report["counts"].add<JsonObject>();

    stats["count_open"] = opening_door_count;
    stats["count_key_open"] = key_open_count;
    stats["count_denied"] = door_denied_count;
    stats["count_button_open"] = button_open_count;
    stats["count_button_unlock"] = unlock_count;
    stats["count_button_to_passstate"] = pass_count;
    stats["count_unexpected_alerts"] = alert_count;
  });

  // Increase LED current to 2/4 of max (default is 1/4, Imax=37mA) to
  // brighten up our button LEDs, potentially at the expensive of the
  // invisible LEDs inside the unit.
  //
  Wire.beginTransmission(0x58);
  Wire.write(0x11);
  Wire.write(2);
  Wire.endTransmission();

  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  Log.printf("Booted: %s " __DATE__ " " __TIME__ "\n", FILE2FIRMWARE(__FILE__));
}

void loop() {
  bool in_open = node.machinestate.state() == BUZZING || node.machinestate.state() == BRACKET;
  bool motorlock_out = doorstate != NIGHT_LOCK || node.machinestate.state() == BUZZING;
  bool solenoid_out = in_open || doorstate == UNLOCKED;

  bool green_led = !solenoid_out;
  bool red_led = (doorstate != NIGHT_LOCK);
  bool pass_led = (doorstate == UNLOCKED);

#if 1
  static unsigned long lst = 0;
  if (millis() - lst > 10 * 1000) {
    lst = millis();

    Debug.printf("State: %10s",
                 doorstate_label[doorstate]);
    Debug.printf(" (%d%s)",
                 doorstate,
                 forced_night ? ",Forced" : "");
    Debug.printf("[%s]",
                 node.machinestate.label());
    Debug.printf(", %u seconds",
                 0 + node.machinestate.secondsInThisState());
    Debug.printf(" - %s/%s -- Motor=%d, Solenoid=%d, Open=%d",
                 isWorkingHours() ? "working-hours" : "outside-working-hours",
                 isDaytime() ? "daytime" : "nighttime",
                 motorlock_out, solenoid_out, in_open);
    Debug.printf(", RED=%s, GREEN=%s, PASS=%s, ",
                 red_led ? "LIT" : "off",
                 green_led ? "LIT" : "off",
                 pass_led ? "LIT" : "off");

    struct tm ts;
    if (!getLocalTime(&ts))
      Log.println("Failed to obtain local time");
    else
      Debug.println(&ts, "Localtime=%A, %B %d %Y %H:%M:%S zone %Z %z");
  };
#endif

  node.setMonitoredOutput(DAY_OPEN, motorlock_out);
  node.setMonitoredOutput(DAY_SOLENOID, solenoid_out);
  node.buzzer(in_open);

  // Pressing the red button will `force' the door closed, even during
  // working hours until either the end of the working day or until the
  // Green button is pressed long.
  if (forced_night && !isWorkingHours())
    forced_night = false;

  // light the LED when it makes sense to press them. We may need to do the
  // exact opposite - i.e. let the LED not reflect the action you can do
  // with the button - but the state that the button brought the lock into.
  //
  // We undulate them to make it easy to spot a hung node & to give the
  // impression of 'action'
  //
  expandedAnalogWrite(LED_BUTTON_GREEN, green_led ? (((doorstate == NIGHT_LOCK) || forced_night) ? 255 : hearthbeat()) : 0);
  expandedAnalogWrite(LED_BUTTON_RED, red_led ? hearthbeat() : 0);
  expandedAnalogWrite(LED_PASSAGE_MODE, pass_led ? hearthbeat() : 0);

  if ((doorstate != NIGHT_LOCK) && (!isWorkingHours()) && (node.machinestate.backgroundTaskOk()) && (node.machinestate.secondsInThisState() > 1800)) {
    Log.println("Not seen anyone for over half an hour; going to night lock as it is outside working hours");
    doorstate = NIGHT_LOCK;
  };
  {
    static unsigned long lst = millis();

    if (millis() - lst > 1000 || last_doorstate != doorstate) {
      node.updateDisplayStateMsg(doorstate_label[doorstate], 2);
      lst = millis();
      last_doorstate = doorstate;
    }
  };

  if (forced_night && (doorstate == NIGHT_LOCK) && !isDaytime() && (node.machinestate.secondsInThisState() > 1800)) {
    forced_night = false;
    Log.println("On night lock; disabling forced night as it is now night.");
  };

  node.loop();
}
