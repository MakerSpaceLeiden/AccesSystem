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

  Main Settings:

  Board:             "Olimex POE board"
  Core Debug Level:  "None"
  Flash Size:        "4MB (32Mb)"
  Partition Scheme:  "Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)"
  Upload Speed:      "921600"

  Hardwared to an MFRC522 card and 4 buttons/led's.

*/
#include <ACNode.h>
#include <REST/ACRestNode.h>
#include <REST/PaymentAPI.h>
#include <RFID/RFID_MFRC522.h>

// For olimex
#include <ETH.h>
#include <WiredEthernet.h>

#define MACHINE "sumup"

ACNodeRest node = ACNodeRest(MACHINE);
#ifdef OTA_PASSWD_HASH
OTA ota(OTA_PASSWD_HASH);
#endif

// Wiring
#define RFID_RESET    (00)
#define RFID_MISO     (03) // 03 
#define RFID_MOSI     (02) // 02
#define RFID_CS       (15) // labeled SDA on the blue boards
#define RFID_CLK      (32)
#define RFID_IRQ      (34)

#define BUTTON_1      (05)  // label  5 euro
#define BUTTON_2      (04)  // label 10 euro
#define BUTTON_3      (14)  // label 25 euro
#define BUTTON_4      (13)  // label 50 euro

RFID_MFRC522 * rfid;
PaymentAPI * paymentAPI;

const int N_PINS = 4;
uint8_t GPIO_PIN[N_PINS] = { BUTTON_1, BUTTON_2, BUTTON_3, BUTTON_4 };
const int NO_PIN_SELECTED = -1;
int pin_selected = NO_PIN_SELECTED;

// reporting
unsigned long denied_count = 0;
unsigned long card_swiped_count = 0;
unsigned long requests_failed = 0;
float amount_requested_paid = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__);

  // Change to something like debug or test
  // if you want to send all output to a different
  // set of MQTT channels.
  //
  // node.set_mqtt_prefix("test");

  // Setup ethernet == default Olimex
  WiFi.onEvent(WiFiEvent);
  ETH.begin();

#ifdef OTA_PASSWD_HASH
  node.addHandler(&ota);
#else
#error "You propablly do not want to deploy without OTA"
#endif

  // Propably should be moved into ACNode as it is so generic.
  //
  rfid = new RFID_MFRC522(RFID_CS, RFID_RESET, RFID_IRQ, RFID_CLK, RFID_MISO, RFID_MOSI);
  node.addHandler(rfid);

  rfid->onSwipe([&](const char *tag) -> ACBase::cmd_result_t {
    Debug.println("Handling swipe - asking for name/mapping owner");
    return node._restAPI->handleTagSwipe(tag);
  });

  // _restAPI isunprotected for now; may change
  paymentAPI = new PaymentAPI(node._restAPI, true /* wants pricelist */);
  node.addHandler(paymentAPI);

  node.onApproval([](const char* machine) {
    Serial.printf("Approval callback called\n");

    card_swiped_count++;
    if (pin_selected == NO_PIN_SELECTED) {
      Log.printf("Card swiped; but no amount to pay selected\n");
      return;

    };
    if (paymentAPI->pricelist == NULL) {
      Log.printf("Card swiped; but we have no pricelist (yet)\n");
      return;
    }

    for (auto it = paymentAPI->pricelist->items.begin(); it != paymentAPI->pricelist->items.end(); ++it) {
      const char * p = it->name.c_str();
      Serial.printf("Checking against %s\n", it->name.c_str());

      if (strncmp(p, "button ", 7))
        continue;

      if (atoi(p + 7) == pin_selected + 1) {
        Debug.printf("Card swiped by %s, button %d pressed: %s: %s. Triggering %.2f payment RQ on the Solo terminal",
                     node.lastApproved()->name.c_str(),
                     pin_selected + 1, it->name.c_str(), it->desc.c_str(), it->price);

        // Have the CRM file a payment request. We trust the
        // uid string (currently a number) to be URL safe.
        //
        char buff[256];
        snprintf(buff, sizeof(buff), "userid=%s&amount=%.2f",
                 node.lastApproved()->uid, it->price);

        if (node._restAPI->rest(SUMUP_URL, String(buff))) {
          Debug.printf("SOLO terminal asking for %.2f payment by %s now.",
                       it->price, node.lastApproved()->name);
          amount_requested_paid += it->price;
        } else {
          Debug.println("SOLO terminal could not be activated, network issue to CRM server?");
          requests_failed++;
        };
        pin_selected = NO_PIN_SELECTED;
        return;
      }
      Debug.printf("SKU %d did not match button %d, skipped", pin_selected + 1);
    }
    Log.printf("Card swiped; button %d selected, but not on the pricelist", pin_selected + 1);
  });

  node.onDenied([](const char* machine) {
    Log.println("Denied");
    pin_selected = NO_PIN_SELECTED;
    denied_count++;
  });

  node.onReport([](JsonObject & report) {
    report["card_swiped_count"] = card_swiped_count;
    report["denied_count"] = denied_count;
    report["requests_failed"] = requests_failed;
    report["amount_requested_paid"] = amount_requested_paid;
  });


  // node.set_debug(true);
  // node.set_debugAlive(true);
  node.begin();

  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__);
}

void loop() {
  static unsigned long _lst_change = 0;
  node.loop();

  if ((millis() > _lst_change + 50 * 1000) && (pin_selected  != NO_PIN_SELECTED)) {
    Log.println("Resetting buttons; idle for too long");    
    digitalWrite(GPIO_PIN[pin_selected], HIGH);
    pin_selected = NO_PIN_SELECTED;
  };

  switch (node.machinestate) {
    case MachineState::WAITINGFORCARD:
      // first scan; then set - so we can take the first and
      // thus ignore multi-presses.
      for (int i = 0; i < N_PINS; i++) {
        if (pin_selected != i) {
          if (digitalRead(GPIO_PIN[i]) == LOW) {
            pin_selected = i;
            _lst_change = millis();
            Debug.printf("Button %d selected\n", i + 1);
            break;
          };
        };
      };
      for (int i = 0; i < N_PINS; i++) {
        pinMode(GPIO_PIN[i], (pin_selected == i) ? OUTPUT_OPEN_DRAIN : INPUT);
        digitalWrite(GPIO_PIN[i], pin_selected == i ? LOW : HIGH);
      }
      break;
    default:
      pin_selected = NO_PIN_SELECTED;
      break;
  }
};
