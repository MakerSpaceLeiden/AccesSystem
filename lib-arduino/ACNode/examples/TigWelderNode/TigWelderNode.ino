/*
 Copyright 2024 Stichting Makerspace Leiden, the Netherlands.
 
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at
 
 http://www.apache.org/licenses/LICENSE-2.0
 
 Unless required by applicable law or agreed to in writing, softwareM
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF
 ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 
 Board: v1.11 / black; with 12VAC transformer on WiFi

 Compile settings:
 - ESP32-WROOM-DA Module (or ESP32 Dev) with minial SPIFFs
 Wiring:
 - https://wiki.makerspaceleiden.nl/mediawiki/index.php/Node_TigWelder
  QR code shown:
 - https://wiki.makerspaceleiden.nl/mediawiki/index.php/QR_tigwelder
 
 */
#include <BlackNodev111.h>

#include "use_counters.h" // i2c pressure sensor
#include "XGZP6897D-i2c.h" // i2c pressure sensor

// Image of closing valve/torch trigger
#include "valve-icon.h"

#ifndef MACHINE
#define MACHINE             "tigwelder"
#endif

// Sensors
#define WELDING_VOLTAGE     (node.OPTO0) // Detect voltage across the flow valve operated by the torch pushbutton.
#define POWER_VOLTAGE       (node.OPTO1) // Detect that the device is powered on (post on/off front switch)

// Outputs:
#define POWER_GPIO          (node.OUT0)   // Controls mains power to on/off switch and the cutoff valve.
#define SOLENOID_GPIO       (node.OUT1)   // Open/close the 12V-DC torch operated valve inside the.

// Generate with 'echo -n Password | openssl md5 or
// use https://www.md5hashgenerator.com/. No \0,
// cariage return or linefeed  at the end of the
// password; just the characters of the password
// itself.
//
// #define OTA_PASSWD_HASH  "0f475732f6c1a632b3e161160be0cfc5" // the MD5 of "SomethingSecrit"
//
#ifndef OTA_PASSWD_HASH
#error "An OTA password hash(md5) MUST be set. Sorry."
#endif
const char ota_password_hash[] = OTA_PASSWD_HASH;

BlackNodev111 node = BlackNodev111(MACHINE, WIFI_NETWORK, WIFI_PASSWD);
IODebounce *powerDetect, *weldingDetect;

MachineState::machinestate_t UNLOCKED; // Can be powered on with front button
MachineState::machinestate_t WELDING;  // Button on torch is pressed.
MachineState::machinestate_t WAITING_FOR_VALVE;  // Waiting for the valve to be closed.
MachineState::machinestate_t CHECK_VALVE_CLOSED;  // Check if the valvue is closed.
MachineState::machinestate_t SWAPPING_BOTTLE, THANKS_BOTTLE;
MachineState::machinestate_t IDLE_POWER_OFF; // state reached on idle (as opposed to a normal power off)

const unsigned int MAX_SECS_IDLE  = 2*3600; // Auto off timeout, in seconds; when not used, etc.
const unsigned int MAX_SECS_POWERON = 120; // Time to actually turn on the machine; before we go to safe again.

// Give the user 2 minutes to close the valve; while we check occasionally.
//
const unsigned int LET_USER_DO_IT_TIMEOUT_MS  = 30 * 1000;

// How long to let the solenoid bleed the gas before we
// expect the pressure to drop enough to notice.
 const unsigned int BLEED_TIME_MS = 1000;

unsigned long power_fault = 0, normal_poweroff = 0, bad_poweroff = 0, idle_poweroff = 0;

XGZP6897D *pressureSensor;
#define KpressureSensor (8) // 1MPa sensor
#define PRESSURE_VALVE_CLOSED_LIMIT (6*1000 /* Pascal */) // below this pressure valve is assumed closed.
#define HYSTERESIS (1+0.10) // 10% hysteresis either way -- to prevent flapping.

class MachineDeck : public Deck {
public:
    MachineDeck(BlackNodev111 * node) : Deck(node) {};
    void render_pane(bool refresh) {
        if (!refresh)
            return;
        _display->clearDisplay();
        _display->print_centred(MACHINE);
        _display->updateDisplay("Bottle","SWAP","NEXT",true);
        _display->setCursor(0,12);
                
        _display->printf("Welding :%lu [mins]\n", (wr.welding_timer+30UL)/60UL);
        if (wr.bottle_date) {
            char buff[20];
            struct tm *p = localtime((time_t*)&(wr.bottle_date));
            snprintf(buff,sizeof(buff),"%04d/%02d/%02d",
                     p->tm_year+1900, p->tm_mon+1, p->tm_mday);
            _display->printf("Bottle  :%s\n", buff);

            snprintf(buff,sizeof(buff),"%02d:%02d",
                     p->tm_hour, p->tm_min);
            _display->printf("         %s by\n", buff);
            _display->printf("         %s\n", wr.changed_by);
        };
    }
};
MachineDeck machineDeck(&node);

class GasDeck : public Deck {
public:
    GasDeck(BlackNodev111 * node) : Deck(node) {};
    void render_pane(bool refresh) {
        char buff[64];

        if (refresh) {
                _display->clearDisplay();
                _display->updateDisplay("gas","","NEXT",true);
        }
        if (millis() - lst > 500){
        lst = millis();

        float pressure = pressureSensor->getPressureInPa();

        int line = 0;
        if (pressure == pressureSensor->ERRVAL)
            _display->updateDisplayStateMsg("Press : FAILED",line++);
        else {
            snprintf(buff,sizeof(buff),"Press :%6.1f kPa", pressure/1000.);
            _display->updateDisplayStateMsg(buff,line++);
            snprintf(buff,sizeof(buff),"        %6.2f bar", pressure/1000000.);
            _display->updateDisplayStateMsg(buff,line++);
        };

        float temperature = pressureSensor->getTemperatureInC();

        if (temperature == pressureSensor->ERRVAL)
            _display->updateDisplayStateMsg("Temp  : FAILED",line++);
        else {
            snprintf(buff,sizeof(buff),"Temp  : %6.1f %cC ", temperature, ADAFRUIT_GFX_DEGREE_SYMBOL);
            _display->updateDisplayStateMsg(buff,line++);
        };
        };
        _display->display();
    }
private:
    unsigned long lst = 0;
};
GasDeck gasDeck(&node);

class ReplaceBottleDeck : public Deck {
public:
    ReplaceBottleDeck(BlackNodev111 * node) : Deck(node) {};
    void render_pane(bool refresh) {
        _display->updateDisplay("swap","CANCEL","",true);
        _display->print_centred("replace bottle",false);
        _display->print_centred("and swipe tag",false);
    };
};
ReplaceBottleDeck replaceBottleDeck(&node);

void setup() {
    Serial.begin(115200);
    Log.setTimestamp(true); Log.setIdentifier("LOG");
    Debug.setTimestamp(true);Debug.setIdentifier("DBG");
    
    Log.printf("\nBooting(): %s " __DATE__ " " __TIME__ "\n", FILE2FIRMWARE(__FILE__));

    expandedPinMode(SOLENOID_GPIO, OUTPUT);
    node.setMonitoredOutput(SOLENOID_GPIO, 0); // value in the off, solenoid closed

    expandedPinMode(POWER_GPIO, OUTPUT);
    node.setMonitoredOutput(POWER_GPIO, 0); // relay in the off/safe position
    
    pressureSensor = new XGZP6897D(KpressureSensor);
    
    // After swiping a tag - the tig welder is powered; but the user
    // still needs to operate the switch on the front. This gives the
    // user 30 seconds to do that.
    UNLOCKED = node.machinestate.addState("Switch Welder on", LED::LED_ON,
                                          MAX_SECS_POWERON * 1000,  MachineState::WAITINGFORCARD, false);

    // Detect that we're actually welding; resets any auto-off timers. And also we
    // keep track of the minutes of gas-flow; in the hope that we can somewhat
    // automate the logistics around (new) gas bottles and payment at some point
    // in the future.
    //
    WELDING = node.machinestate.addState("Welding", LED::LED_ON,
                                         MachineState::NEVER, MachineState::WAITINGFORCARD, false);
                                         
    // Still ask the user to close the valve - but no longer test (as to not loose gas); and
    // expect the user to press the OK button or bleed via the torch.
    //
    WAITING_FOR_VALVE = node.machinestate.addState("Close Valve", LED::LED_ON,
                                         MachineState::NEVER, CHECK_VALVE_CLOSED, false);

    // Ask the user to close the valve; and periodically bleed the gas via the solenoid
    // overrride to see if the pressure dropped.
    CHECK_VALVE_CLOSED = node.machinestate.addState("Close Valve!", LED::LED_ON,
                                         LET_USER_DO_IT_TIMEOUT_MS, WAITING_FOR_VALVE, false);
                                         
                                         
    SWAPPING_BOTTLE = node.machinestate.addState("Swap Bottle", LED::LED_ON,
                                         600 * 1000, MachineState::WAITINGFORCARD, false);
    THANKS_BOTTLE  = node.machinestate.addState("Thanks !", LED::LED_ON,
                                         3 * 1000, MachineState::WAITINGFORCARD, false);
                                         
    // Normally the powered state goes straight to waiting for card. Change this so we
    // can have a valve close logic.
    //
    IDLE_POWER_OFF = node.machinestate.addState("Powering off", LED::LED_ON,
                                         MachineState::NEVER, MachineState::WAITINGFORCARD, false);

    node.machinestate.setTimeoutState(POWERED,IDLE_POWER_OFF);
    node.machinestate.setTimeout(POWERED, MAX_SECS_IDLE*1000);
        
    expandedPinMode(POWER_VOLTAGE, INPUT);
    powerDetect = new IODebounce(POWER_VOLTAGE);
    powerDetect->setDigitalReadFunction(&expandedDigitalRead);
    powerDetect->setCallback([](const int newState) {
        // Debug.println(newState ? "OPTO2: Power OFF" : "OPTO2: Power ON");
        expandedAnalogWrite(node.LEDC, (!newState) ? 255 : 0);
        if ((node.machinestate == MachineState::CHECKINGCARD || node.machinestate == MachineState::WAITINGFORCARD) && newState == LOW) {
            Log.println("Alert: Power observed while " MACHINE " should be off");
            node.machinestate = FAULTED;
            power_fault++;
        }
        else if (node.machinestate == UNLOCKED && newState == LOW) {
            Log.println("Machine switched on with front switch");
            node.machinestate = POWERED;
        }
        else if (node.machinestate == FAULTED && newState == HIGH) {
            Log.println("Alert: Odd powerstate cleared.");
            node.machinestate = MachineState::WAITINGFORCARD;
        }
        else if (node.machinestate == POWERED && newState == HIGH) {
            Log.println("Normal poweroff with switch on front.");
            node.machinestate = CHECK_VALVE_CLOSED;
            normal_poweroff++;
            welding_save(true);
        }
        else if (node.machinestate == WELDING && newState == HIGH) {
            Log.println("Odd, machine switched off while welding?!");
            node.machinestate = CHECK_VALVE_CLOSED;
            bad_poweroff++;
            welding_save(true);
        }
    }, CHANGE);
    node.addHandler(powerDetect);
    
    expandedPinMode(WELDING_VOLTAGE, INPUT);
    weldingDetect = new IODebounce(WELDING_VOLTAGE);
    weldingDetect->setDigitalReadFunction(&expandedDigitalRead);
    weldingDetect->setCallback([](const int newState) {
        // Debug.println(newState ? "OPTO1: No gas flow/solenoid off" : "OPTO1: gas flow/solenoid on");
        expandedAnalogWrite(node.LEDB, (!newState) ? 255 : 0);
        static unsigned long lst = 0;
        if (node.machinestate == POWERED && newState == LOW) {
            // Debug.println("We're welding");
            lst = millis();
            node.machinestate = WELDING;
        } else if (node.machinestate == WELDING && newState == HIGH) {
            // Debug.println("Done welding.");
            unsigned long wt = millis() - lst;
            wr.welding_timer += (wt+500UL)/1000UL;
            node.machinestate = POWERED;
        }
    }, CHANGE);
    node.addHandler(weldingDetect);

    node.setOTAPasswordHash(ota_password_hash);
    node.set_mqtt_prefix("ac");
    node.set_master("master");
    
    node.setNodeDeck(&gasDeck);
    node.setNodeDeck(&machineDeck);
    
    node.onReport([](JsonObject & report) {
        char * p = __FILE__;
        char * q = rindex(p,'/');
        if (q) p = q;
        char tmp[256];
        snprintf(tmp,sizeof(tmp),"%s %s %s", FILE2FIRMWARE(__FILE__), __DATE__,__TIME__);
        report["fw"] = tmp;
        report["power_fault"] = power_fault;
        report["bad_poweroff"] = bad_poweroff;
        report["normal_poweroff"] = normal_poweroff;
        report["idle_poweroff"] = idle_poweroff;
        report["welding_secs"] = wr.welding_timer;
    });
    
    node.setOnChangeCallback(MachineState::ALL_STATES, [&](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
        if (last == POWERED && current == MachineState::WAITINGFORCARD) {
            Log.println("Power switched off; waiting for valve to be closed");
            node.machinestate = CHECK_VALVE_CLOSED;
        } else
        if (current == IDLE_POWER_OFF) {
            // Special case for the Welder - we do not want to silently
            // switch off after an hour of non-use; but actually alert people
            // to the fact that this happened; so someone hopefully closes
            // the valve.
            //
            Log.println("Power switched off after a long idle time; waiting for valve to be closed");
            node.machinestate = WAITING_FOR_VALVE;
            idle_poweroff++;
        } else
        if (current == CHECK_VALVE_CLOSED) {
            Log.println("Waiting & checking if the valve is closed");
            _display->clearDisplay();
            _display->drawCentredBitmap(valve_icon, 128, 64, SH110X_WHITE);
            _display->display();
        } else
        if (current == WAITING_FOR_VALVE) {
            Log.println("Waiting (forever) for valve to be closed");
            _display->updateDisplay("CLOSE VALVE","","OK",true);
            node.updateDisplayStateMsg("and press OK",0);
            node.updateDisplayStateMsg("to confirm",1);
        }
        else if (node.machinestate == UNLOCKED) {
            node.updateDisplayStateMsg("Switch on",0);
            node.updateDisplayStateMsg("with front switch",1);
        }
        else if (node.machinestate == SWAPPING_BOTTLE) {
            replaceBottleDeck.display();
        };
    });
    
    node.onApproval([](const char *machine) {
        // We allow 'taking over this machine while it is on' -- hence this check for
        // if it is powered; and in that case -also- accepting a new approval.
        //
        Log.printf("onApproval callback state: %s\n", node.machinestate.label());
        if (node.machinestate == SWAPPING_BOTTLE) {
            ApprovalEntry * e = node.lastApproved();
            
            const char * shortName = e ? e->shortName.c_str() : "Unknown";
            welding_bottle_reset(shortName);
            
            const char * name = e ? e->name.c_str() : "Unknown";
            Log.printf("Bottle reported swapped by %s, used for %d seconds\n",
                       name, wr.welding_timer);
                       
            node.machinestate = THANKS_BOTTLE;
            return;
        } else
        if ((node.machinestate != POWERED) &&
            (node.machinestate != MachineState::CHECKINGCARD) &&
            (node.machinestate != SCREENSAVER) &&
            (node.machinestate != MachineState::WAITINGFORCARD)
            ) {
            Log.println("Rejecting tag swipe; not expecting one");
            node.buzzerErr();
            return;
        };
        // Skip the unlocked; now turn the machine on state if
        // we're already powered on by this time (or a previous)
        // users left the front switch in the 'on'' position.
        //
        if (node.machinestate != POWERED)
            node.machinestate = powerDetect ? UNLOCKED : POWERED;
    });
    
    node.setMenuCallback([&](const int newState) -> bool {
        if (node.machinestate == WAITING_FOR_VALVE) {
            Log.println("Valve confirmed closed by button press");
            node.machinestate = CHECK_VALVE_CLOSED;
        } else
        if (node.machinestate == POWERED) {
            node.machinestate.resetTimeout();
            node.updateDisplayStateMsg("", 2);   // clear any timer
            return true;
        }
        return false;
    },FALLING);
    
    node.setYesCallback([&](const int newState) -> bool {
        if (node.machinestate == POWERED) {
            node.machinestate.resetTimeout();
            node.updateDisplayStateMsg("", 2); // clear any timer
            return true;
        }
        return false;
    },FALLING);
 
    node.setOffCallback([&](const int newState) -> bool {
        if (node.machinestate == INFODISPLAY && (node.currentDeck() == &machineDeck)) {
            Log.println("Swapping bottle process initiated");
            node.machinestate = SWAPPING_BOTTLE;
            // return true;
        } else
        if (node.machinestate == SWAPPING_BOTTLE) {
            Log.println("Swapping bottle cancled");
            node.machinestate = MachineState::WAITINGFORCARD;
        } else
        if (node.machinestate == UNLOCKED) {
            Log.println("Power-on canceled by button press");
            node.machinestate = MachineState::WAITINGFORCARD;
        } else
        if (node.machinestate == POWERED) {
            node.machinestate.resetTimeout();
            node.updateDisplayStateMsg("", 2); // clear any timer
            return true;
        }
        return false;
    },FALLING);
    
    node.begin();
    welding_init();

    Log.printf("Starting loop(): %s " __DATE__ " " __TIME__ "\n", FILE2FIRMWARE(__FILE__));
}

void loop() {
    node.loop();

    bool r = (node.machinestate == UNLOCKED) || (node.machinestate == POWERED) || (node.machinestate == WELDING);
    node.setMonitoredOutput(POWER_GPIO, r); // needs to be high to engage the relay
    
    // We normally do not operate the torch-gas solenoid in the machine; so it
    // should be off.
    //
    node.setMonitoredOutput(SOLENOID_GPIO, LOW);

    // Disco test - for checking out hardware
    if (0) {
        static unsigned long lst = millis();
        static int i = 0;
        if (millis() - lst > 300) {
                lst = millis();
                expandedAnalogWrite(node.LEDA, (i % 8 == 0) ? 255 : 0);
                expandedAnalogWrite(node.LEDB, (i % 8 == 1) ? 255 : 0);
                expandedAnalogWrite(node.LEDC, (i % 8 == 2) ? 255 : 0);
                expandedAnalogWrite(node.LEDD, (i % 8 == 3) ? 255 : 0);
                expandedAnalogWrite(node.LEDE, (i % 8 == 4) ? 255 : 0);
                expandedDigitalWrite(node.OUT0, i % 8 == 5);
                expandedDigitalWrite(node.OUT1, i % 8 == 6);
                expandedDigitalWrite(node.LED_INDICATOR, i % 8 == 7);
                i++;
                Debug.println((i&1) ? "ON" : "OFF");
        };
    };

    // for checking the sensors/connections.
    if (0) {
        static unsigned long lst = millis();
        if (millis() - lst > 1000) {
            lst = millis();
            float pressure = pressureSensor->getPressureInPa();
            Debug.printf("Opto 1: %d 2: %d 3: %d 4:%d P:%12.1f",
                expandedDigitalRead(node.OPTO0),expandedDigitalRead(node.OPTO1),
                expandedDigitalRead(node.OPTO2),expandedDigitalRead(node.OPTO3),
                pressure);
            Debug.printf(" -- Welding: %d/%s Power: %d/%s\n",
                powerDetect->state(),
                powerDetect->state() ? "OFF" : "ON",
                weldingDetect->state(),
                weldingDetect ? "OFF" : "ON");
        };
    };

    static float pressure = 0;
    static bool valveOpen = false;
    static unsigned long lastValveOpen = 0;
    static char pressbuff[12];
    {
            static unsigned lst = 0;
            if (millis() - lst > 200) {
                lst = millis();
                float p = pressureSensor->getPressureInPa();
                if (p == pressureSensor->ERRVAL) {
                    static unsigned lst = 0;
                    if (millis() - lst > 60*1000) {
                        Log.printf("Failed to read pressure sensor\n");
                        lst = millis();
                        };
                    expandedDigitalWrite(node.LED_INDICATOR, HIGH);
                    snprintf(pressbuff,sizeof(pressbuff),"press-fail");
                }  else {
                    if (pressure == 0) pressure = p;
                    pressure = (pressure*2. + p)/3.;
                    snprintf(pressbuff,sizeof(pressbuff),"%5.0f kPa", pressure/1000.);
                }
                if (node.machinestate == MachineState::WAITINGFORCARD ||
                    node.machinestate == POWERED ||
                    node.machinestate == WELDING
                ) node.updateDisplayStateMsg(pressbuff, 2);

                if (pressure > PRESSURE_VALVE_CLOSED_LIMIT * HYSTERESIS) {
                    valveOpen = true;
                    lastValveOpen = millis();
                } else
                if (pressure < PRESSURE_VALVE_CLOSED_LIMIT / HYSTERESIS) {
                    valveOpen = false;
                };
            };
            expandedAnalogWrite(node.LEDD, valveOpen ? 255 : 0);
    };
    
    // If we are in some idle mode - check the pressure every 5 seconds or so.
    //
    if (node.machinestate == MachineState::WAITINGFORCARD  ||
        node.machinestate == SCREENSAVER  ||
        node.machinestate == MachineState::OUTOFORDER)
    {
        static unsigned lst = 0;
        if (millis() - lst > 5*1000) {
            lst = millis();
            if (valveOpen) {
                Log.printf("Detected pressure (%.0f kPa)- assuming valve is open\n", pressure / 1000.);
                node.machinestate = WAITING_FOR_VALVE;
            }
        };
    } else
    if (node.machinestate == WAITING_FOR_VALVE) {
        if (!valveOpen) {
            Log.printf("Detected pressure drop (to %.1f kPa) - assuming valve is now closed\n", pressure / 1000.);
            node.machinestate = MachineState::WAITINGFORCARD;
            return;
        };
        static unsigned lst = millis();
        if (millis() - lst > 500) {
            lst = millis();
            node.buzzerOk();
        };
    } else if (node.machinestate ==  CHECK_VALVE_CLOSED) {
        if (!valveOpen) {
            Log.printf("Detected pressure drop (to %.1f kPa) - assuming valve is now closed\n", pressure / 1000.);
            node.machinestate = MachineState::WAITINGFORCARD;
            return;
        };
        static unsigned lst = 0;
        if (millis() - lst > 1000) {
            node.buzzer(true);

            // Check to see if the user actually closed the valve by
            // opening the torch valve for BLEED_TIME_MS.
            //
            node.setMonitoredOutput(SOLENOID_GPIO, HIGH);

            // 12v check valve needs to be also open for this check
            // to work.
            node.setMonitoredOutput(POWER_GPIO, HIGH);
 
            expandedAnalogWrite(node.LEDC,255);
            delay(BLEED_TIME_MS);
            expandedAnalogWrite(node.LEDC,0);
            
            node.setMonitoredOutput(SOLENOID_GPIO, LOW);
            node.buzzer(false);
            lst = millis();
        }
    } else if (node.machinestate == UNLOCKED) {
        String left = node.machinestate.timeLeftInThisState();
        node.updateDisplayStateMsg("Auto off in " + left, 2);
    };

}
