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

ButtonDebounce *powerDetect, *weldingDetect;

MachineState::machinestate_t UNLOCKED; // Can be powered on with front button
MachineState::machinestate_t WELDING;  // Button on torch is pressed.
MachineState::machinestate_t WAITING_FOR_VALVE;  // Waiting for the valve to be closed.
MachineState::machinestate_t CHECK_VALVE_CLOSED;  // Check if the valvue is closed.
MachineState::machinestate_t SWAPPING_BOTTLE, THANKS_BOTTLE;
MachineState::machinestate_t IDLE_POWER_OFF; // state reached on idle (as opposed to a normal power off)

const unsigned int MAX_SECS_IDLE  = 2*3600; // Auto off timeout, in seconds

// Give the user 2 minutes to close the valve; while we check occasionally.
//
const unsigned int LET_USER_DO_IT_TIMEOUT_MS  = 30 * 1000;

// How long to let the solenoid bleed the gas before we
// expect the pressure to drop enough to notice.
 const unsigned int BLEED_TIME_MS = 250;

unsigned long power_fault = 0, normal_poweroff = 0, bad_poweroff = 0, idle_poweroff = 0;

XGZP6897D *pressureSensor;
#define KpressureSensor (8) // 1MPa sensor
#define PRESSURE_VALVE_CLOSED_LIMIT (6*1000 /* Pascal */) // below this pressure valve is assumed closed.
#define HYSTERESIS (1+0.10) // 10% hysteresis either way -- to prevent flapping.

int valve_check_counter = 0;
const int MAX_VALVE_CHECKS = 10;

class MachineDeck : public Deck {
public:
    MachineDeck(BlackNodev111 * node) : Deck(node) {};
    void render_pane(bool refresh) {
        _display->clearDisplay();
        _display->print_centred(MACHINE);
        _display->printf("On/Off  :%s\n", powerDetect->state() ? "on" : "off");
        _display->printf("Welding :%s\n", weldingDetect->state() ? "pressed" : "off");
        _display->printf("        :%d [mins]\n", 0.5 + wr.welding_timer/60.);
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
class GasDeck : public Deck {
public:
    GasDeck(BlackNodev111 * node) : Deck(node) {};
    void render_pane(bool refresh) {
        if (millis() - lst < 500)
            return;
        lst = millis();
        
        float pressure = pressureSensor->getPressureInPa();
        float temperature = pressureSensor->getTemperatureInC();

        _display->updateDisplay("gas","SWAP","NEXT",true);

        _display->setFont(FONT_SMALL);
        _display->setTextColor(SH110X_WHITE);
        _display->setCursor(0,12);
        
        _display->printf("Press: ");
        if (pressure == pressureSensor->ERRVAL)
            _display->printf("FAIL\n");
        else {
            _display->printf("%6.1f kPa\n", pressure/1000.);
            _display->printf("       %6.2f bar\n", pressure/1000000.);
        };
        _display->printf("Temp : ");
        if (temperature == pressureSensor->ERRVAL)
            _display->printf("FAIL\n");
        else
            _display->printf("%6.1f %cC\n", temperature, ADAFRUIT_GFX_DEGREE_SYMBOL);

        _display->display();
    }
private:
    unsigned long lst = millis();
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
                                          30 * 1000,  MachineState::WAITINGFORCARD, false);

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
    node.machinestate.setTimeout(POWERED,60 * 1000);
        
    expandedPinMode(POWER_VOLTAGE, INPUT);
    powerDetect = new ButtonDebounce(POWER_VOLTAGE);
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
            welding_save();
        }
        else if (node.machinestate == WELDING && newState == HIGH) {
            Log.println("Odd, machine switched off while welding?!");
            node.machinestate = CHECK_VALVE_CLOSED;
            bad_poweroff++;
            welding_save();
        }
    }, CHANGE);
    
    expandedPinMode(WELDING_VOLTAGE, INPUT);
    weldingDetect = new ButtonDebounce(WELDING_VOLTAGE);
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
            wr.welding_timer = 0.5 + wt/1000.;
            node.machinestate = POWERED;
        }
    }, CHANGE);
    
    node.setOTAPasswordHash(ota_password_hash);
    node.set_mqtt_prefix("ac");
    node.set_master("master");
    
    node.setNodeDeck(&gasDeck);
    node.setNodeDeck(new MachineDeck(&node));
    
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
            // go idle after an hour of non-use; but actually alert people
            // to the fact that this happened.
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
            const char * name = e ? e->name.c_str() : "Unknown";

            Log.printf("Bottle reported swapped by %s, used for %d seconds\n",
                       name, wr.welding_timer);

            welding_bottle_reset(name);
            node.machinestate = THANKS_BOTTLE;
            return;
        } else
        if ((node.machinestate != POWERED) &&
            (node.machinestate != MachineState::CHECKINGCARD)
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
        if (node.machinestate == INFODISPLAY && (node.currentDeck() == &gasDeck)) {
            Log.println("Swapping bottle process initiated");
            node.machinestate = SWAPPING_BOTTLE;
            return true;
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
    
    welding_init();
    node.begin();
    
    Log.printf("Starting loop(): %s " __DATE__ " " __TIME__ "\n", FILE2FIRMWARE(__FILE__));
}

void loop() {
if (0) {
        static unsigned long lst = millis();
        static int i = 0;
        if (millis() - lst > 3000) {
                lst = millis();
                expandedAnalogWrite(node.LEDA, (i & 1) ? 255 : 0);
                expandedAnalogWrite(node.LEDB, (i & 1) ? 255 : 0);
                expandedAnalogWrite(node.LEDC, (i & 1) ? 255 : 0);
                expandedAnalogWrite(node.LEDD, (i & 1) ? 255 : 0);
                expandedAnalogWrite(node.LEDE, (i & 1) ? 255 : 0);
                expandedDigitalWrite(node.OUT0, i & 1);
                expandedDigitalWrite(node.OUT1, i & 1);
                expandedDigitalWrite(node.LED_INDICATOR, i & 1);
                i++;
                Debug.println((i&1) ? "ON" : "OFF");
        };
        return;
};
    node.loop();

    if (1) {
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

    if (node.machinestate == MachineState::WAITINGFORCARD) {
        static unsigned lst = 0;
        if (millis() - lst > 30*1000) {
            lst = millis();
            float pressure = pressureSensor->getPressureInPa();
            if (pressure == pressureSensor->ERRVAL)
                Log.printf("Failed to read pressure sensor\n");
            else
            if (pressure && (pressure > PRESSURE_VALVE_CLOSED_LIMIT * HYSTERESIS)) {
                Log.printf("Detected pressure (%.1f kPa)- assuming problem with the valve\n", pressure / 1000.);
                node.machinestate = WAITING_FOR_VALVE;
            } else {
                // Debug.printf("Pressure %.1f kPa, %.2f bar\n",pressure / 1000, pressure / 1000000.);
            }
        };
    } else
    if (node.machinestate == WAITING_FOR_VALVE) {
        float pressure = pressureSensor->getPressureInPa();
        if (pressure != pressureSensor->ERRVAL && pressure < PRESSURE_VALVE_CLOSED_LIMIT/HYSTERESIS) {
            Log.printf("Detected pressure drop (to %.1f kPa) - assuming valve is closed\n", pressure / 1000.);
            node.machinestate = MachineState::WAITINGFORCARD;
            valve_check_counter = 0;
            return;
        };
        static unsigned lst = millis();
        if (millis() - lst > 500) {
            lst = millis();
            node.buzzerOk();
        };
    } else if (node.machinestate ==  CHECK_VALVE_CLOSED) {
        float pressure = pressureSensor->getPressureInPa();
        if (pressure != pressureSensor->ERRVAL && pressure < PRESSURE_VALVE_CLOSED_LIMIT/HYSTERESIS) {
            Log.printf("Detected pressure drop (to %.1f kPa) - assuming valve is closed\n", pressure / 1000.);
            node.machinestate = MachineState::WAITINGFORCARD;
            valve_check_counter = 0;
            return;
        };
        static unsigned lst = 0;
        if (millis() - lst > 1500) {
            // Check to see if the user actually closed the valve by
            // opening the torch valve for 0.3 second.
            //
            node.buzzer(true);
            node.setMonitoredOutput(POWER_GPIO, HIGH); // needed - perhaps rewire the phase from main
            node.setMonitoredOutput(SOLENOID_GPIO, HIGH);
            expandedAnalogWrite(node.LEDC,255);
            delay(BLEED_TIME_MS);
            valve_check_counter++;
            expandedAnalogWrite(node.LEDC,0);
            node.setMonitoredOutput(POWER_GPIO, LOW); // needed - perhaps rewire the phase from main
            node.buzzer(false);
            lst = millis();
        }
    } else if (node.machinestate == UNLOCKED) {
        String left = node.machinestate.timeLeftInThisState();
        node.updateDisplayStateMsg("Auto off in " + left, 2);
    };

    expandedAnalogWrite(node.LEDD,(node.machinestate == WAITING_FOR_VALVE) ? 255 : 0);

    bool r = (node.machinestate == UNLOCKED) || (node.machinestate == POWERED) || (node.machinestate == WELDING);
    node.setMonitoredOutput(POWER_GPIO, r); // needs to be high to engage the relay
    
    // We normally do not operate the torch-gas solenoid in the machine; so it
    // should be off.
    //
    node.setMonitoredOutput(SOLENOID_GPIO, LOW);
}
