/*
 Copyright 2015-2016 Dirk-Willem van Gulik <dirkx@webweaving.org>
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

#define BUZZER    (4)   // GPIO pin count.
#define SOLENOID  (5)

#define RFID_SELECT_PIN (6)
#define RFID_RESET_PIN (7)

#include <ACNode.h>

ACNode node = ACNode(true); // Force wired PoE ethernet.

typedef enum {
    BOOTING, SWERROR, OUTOFORDER, NOCONN, // some error - machine disabLED.
    WAITINGFORCARD,             // waiting for card.
    CHECKINGCARD,
    BUZZING,                    // this is where we engage the solenoid.
    REJECTED,
    NOTINUSE
} machinestates_t;

const char *machinestateName[NOTINUSE] = {
    "Software Error", "Out of order", "No network",
    "Waiting for card",
    "Buzzing door",
    "Rejecting noise",
    "== not in use == "
};

unsigned long laststatechange = 0;
static machinestates_t laststate = OUTOFORDER;
machinestates_t machinestate = BOOTING;

OTA ota = OTA("FooBar");		// Over the air config, passowrd.
RFID reader(RFID_SELECT_PIN, RFID_RESET_PIN);
MSL msl = MSL();			// MSL `no security' handler as currently used for the doors.
SIG1 sig1 = SIG1();
Beat beat = Beat();

void setup() {
    
    // Init the hardware and get it into a safe state.
    //
    pinMode(BUZZER, OUTPUT);
    digitalWrite(BUZZER, 0);
    pinMode(SOLENOID, OUTPUT);
    digitalWrite(SOLENOID, 0);
    
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
        if (!strcasecmp("open", cmd)) {
            machinestate = BUZZING;
        }
        else if (!strcasecmp("denied", cmd)) {
            machinestate = REJECTED;
        } else {
            Log.printf("Unhandled command <%s> -- ignored.", cmd);
        }
    });
    
    reader.onSwipe([](const char * tag) {
        Log.printf("Card <%s> wiped - being checked.\n", tag);
        machinestate = CHECKINGCARD;
    });
    
    node.addHandler(ota);
    node.addHandler(reader);
    
#if 0
    // 2017 situation
    node.addSecurityHandler(msl);
#else
    node.addSecurityHandler(sig1);
    node.addSecurityHandler(beat);
#endif
    
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
            digitalWrite(SOLENOID, 0);
            digitalWrite(BUZZER, 0);
            break;
            
        case CHECKINGCARD:
            digitalWrite(BUZZER, ((millis() % 500) < 100) ? 1 : 0);
            if ((millis() - laststatechange) > 5000)
                machinestate = REJECTED;
            break;
            
        case BUZZING:
            digitalWrite(SOLENOID, 1);
            digitalWrite(BUZZER, 1);
            if ((millis() - laststatechange) > 5000)
                machinestate = WAITINGFORCARD;
            break;
            
        case REJECTED:
            digitalWrite(BUZZER, ((millis() % 200) < 100) ? 1 : 0);
            if ((millis() - laststatechange) > 5000)
                machinestate = WAITINGFORCARD;
            break;
            
        case NOCONN:
            digitalWrite(BUZZER, ((millis() % 3000) < 10) ? 1 : 0);
            
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
            digitalWrite(BUZZER, ((millis() % 1000) < 10) ? 1 : 0);
            break;
    };
}

