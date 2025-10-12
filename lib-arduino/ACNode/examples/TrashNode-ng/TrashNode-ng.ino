#include <BlueNodev114.h> // -- this is an olimex board.
#include <limits>

#define MACHINE             "trash"
/*
  Trash node.

  Shows the location of the trash container.
  Alerts members when the container needs to be put outside, based on the agenda.
  A bright light on the node will blink from the start of the chore, until the container has been put outside.

  TODO:
  - Change chore states when changing container location during the chore.
 */

#define OUTSIDE_BUTTON_GPIO (node.IOA)
#define INSIDE_BUTTON_GPIO (node.IOB)
#define OUTSIDE_LED_GPIO (node.IOC)
#define INSIDE_LED_GPIO (node.IOD)
#define REMINDER_LIGHT_GPIO (node.OUT0)

#define BUZZ_TIME     (50) // How long to engage, in milli seconds
#define AUTH_TIMEOUT     (10*1000) // Timeout for authentication

#define CHORE_UPDATE_INTERVAL (3600) // Time between chore updates in seconds
#define CHORE_API_ENDPOINT "/events"
#define CHORE_KEY "Large container outside"

// Generate with 'echo -n Password | openssl md5 or
// use https://www.md5hashgenerator.com/. No \0,
// cariage return or linefeed  at the end of the
// password; just the characters of the password
// itself.
//
// E.g.
//      /bin/echo -n "SomethingSecrit" | openssl md5
// to yeild below:
// #define OTA_PASSWD_HASH  "0f475732f6c1a632b3e161160be0cfc5"
//
#ifndef OTA_PASSWD_HASH
#error "An OTA password hash(md5) MUST be set. Sorry."
#endif
const char ota_password_hash[] = OTA_PASSWD_HASH;

BlueNodev114 node = BlueNodev114(MACHINE, "MakerSpaceLeiden_deelnemers", "M@@K1234");

MachineState::machinestate_t BUZZING, ACTIVE, SET_LOCATION;

unsigned long denied_count = 0;
unsigned long inside_count = 0;
unsigned long outside_count = 0;

typedef enum {
  CONTAINER_INSIDE
, CONTAINER_OUTSIDE
} containerlocation_t;

containerlocation_t currentLocation;
time_t nextCollectionStart = std::numeric_limits<time_t>::max(); // safe default value
time_t nextCollectionEnd = std::numeric_limits<time_t>::max();

IODebounce* insideButton;
IODebounce* outsideButton;

/**
 * Callback for when one of the buttons is pressed.
 * Changes the location of the 
 */
void locationButtonCallback(const int buttonState, containerlocation_t newLocation)
{
  // Only act on button press, not release.
  if (buttonState != 0) return;
  // Do nothing if not in authenticated state
  if (node.machinestate.state() != ACTIVE
   && node.machinestate.state() != SET_LOCATION) {
    Log.println("Tried to change location while not authenticated.");
    return;
  }

  // set location and change state
  currentLocation = newLocation;
  node.machinestate.setState(SET_LOCATION);
  return;
}

/**
 * Update LEDs to reflect current container location.
 */
void locationLEDCallback()
{
  expandedDigitalWrite(INSIDE_LED_GPIO, currentLocation == CONTAINER_INSIDE);
  expandedDigitalWrite(OUTSIDE_LED_GPIO, currentLocation == CONTAINER_OUTSIDE);
}

void maybeUpdateChores()
{
  static time_t lastChoreFetch = 0;
  time_t now = time(NULL);
  if (std::difftime(now,lastChoreFetch) < CHORE_UPDATE_INTERVAL) {
    return;
  }

  lastChoreFetch = now;
  JsonDocument agenda = node._restAPI->get(API_URL CHORE_API_ENDPOINT,"");
  if (agenda.isNull()) {
    Debug.println("Failed to fetch chores!");
    return;
  }

  JsonArray eventList = agenda["data"].as<JsonArray>();
  struct tm choreStartTime, choreEndTime;
  for (JsonVariant event : eventList) {
    if(event["name"] == F(CHORE_KEY)) {
      if(strptime(event["start_datetime"],"%FT%X",&choreStartTime) == NULL) {
        Debug.println("Failed to parse start_datetime from chore" CHORE_KEY);
        return;
      }
      if(strptime(event["end_datetime"],"%FT%X",&choreEndTime) == NULL) {
        Debug.println("Failed to parse end_datetime from chore" CHORE_KEY);
        return;
      }
      nextCollectionStart = mktime(&choreStartTime);
      nextCollectionEnd = mktime(&choreEndTime);
      return;
    }
  }
}

void updateReminderLight()
{
  bool inside = (currentLocation == CONTAINER_INSIDE);

  time_t now = time(NULL);
  bool collectionSoon = (now > nextCollectionStart && now < nextCollectionEnd);

  expandedDigitalWrite(REMINDER_LIGHT_GPIO, inside && collectionSoon);
}

void setup()
{
  // assume container is inside at start.
  currentLocation = CONTAINER_INSIDE;

  BUZZING = node.machinestate.addState((const char*)"Approved", LED::LED_IDLE,
                                       (time_t)(BUZZ_TIME), // stay in this state for BUZZ_TIME milli Seconds
                                       ACTIVE // after wait for user to select container location
                                      );
  ACTIVE = node.machinestate.addState((const char*)"Select container location", LED::LED_PENDING,
                                       (time_t)(AUTH_TIMEOUT), // let authentication time out after AUTH_TIMEOUT ms
                                       node.machinestate.WAITINGFORCARD // return to waiting for card after timeout.
                                      );
  SET_LOCATION = node.machinestate.addState((const char*)"Container location changed", LED::LED_IDLE,
                                       (time_t)(AUTH_TIMEOUT), // retain authentication when user has interacted
                                       node.machinestate.WAITINGFORCARD);
  node.machinestate.addOnChangeCallback(SET_LOCATION,
    [](auto oldState, auto newState) {locationLEDCallback();});
  
  node.onApproval([](const char *machine) {
    node.machinestate.setState(BUZZING);
    Debug.println("Card accepted. Ready to change container location after a short buzz.");
  });

  node.onDenied([](const char *machine) {
    node.buzzerErr();
    node.machinestate.setState(BUZZING);
    denied_count++;
  });

  insideButton = new IODebounce("insideButton", INSIDE_BUTTON_GPIO, 150 /* mSeconds */);
  insideButton->setCallback([&](const int buttonState) {locationButtonCallback(buttonState, CONTAINER_INSIDE);});
  node.addHandler(insideButton);

  outsideButton = new IODebounce("outsideButton", OUTSIDE_BUTTON_GPIO, 150 /* mSeconds */);
  outsideButton->setCallback([&](const int buttonState) {locationButtonCallback(buttonState, CONTAINER_OUTSIDE);});
  node.addHandler(outsideButton);

  expandedPinMode(OUTSIDE_LED_GPIO, OUTPUT);
  expandedPinMode(INSIDE_LED_GPIO, OUTPUT);
  expandedPinMode(REMINDER_LIGHT_GPIO, OUTPUT);

  node.setOTAPasswordHash(ota_password_hash);
  node.set_mqtt_prefix("ac");
  node.set_master("master");

  node.begin();
  const char * p = __FILE__;
  const char * q = rindex(p, '/');
  Log.printf("Fully booted: %s " __DATE__ " " __TIME__, q ? q + 1 : p);
}

void loop()
{
  // maybeUpdateChores();
  node.loop();
  // updateReminderLight();
}
