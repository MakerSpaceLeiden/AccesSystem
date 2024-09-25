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
 
 Compile settings:
 
 ESP32-WROOM-DA Module (or ESP32 Dev) with minial SPIFFs
 
 QR code shown:
 https://wiki.makerspaceleiden.nl/mediawiki/index.php/QR_tigwelder
 
 Board: v1.11 / black; with 12VAC transformer
 */
#include <BlackNodev111.h>
#include <EEPROM.h>
#include <XGZP6897x.h> // i2c pressure sensor

#ifndef MACHINE
#define MACHINE             "tigwelder"
#endif

// Sensors
#define POWER_VOLTAGE       (node.OPTO0) // Detect that the device is powered on (post on/off front switch)
#define WELDING_VOLTAGE     (node.OPTO1) // Detect voltage from the pushbutton in the torch.

// Outputs:
#define POWER_GPIO          (node.OUT0)   // Controls mains power to on/off switch
#define VALVE_GPIO          (node.OUT1)   // Open/close the 12V-DC gas valve; when low; valve is shut

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

// Machine is ACPowered.
BlackNodev111 node = BlackNodev111(MACHINE, WIFI_NETWORK, WIFI_PASSWD);

ButtonDebounce *powerDetect, *weldingDetect;

MachineState::machinestate_t UNLOCKED; // Can be powered on with front button
MachineState::machinestate_t WELDING;  // Button on torch is pressed.
MachineState::machinestate_t WAITING_FOR_VALVE;  // Waiting for the valve to be closed.

const unsigned int MAX_SECS_IDLE  = 2*3600; // Auto off timeout

unsigned long power_fault = 0, normal_poweroff = 0, bad_poweroff = 0, idle_poweroff = 0;

XGZP6897x *pressureSensor;
#define KpressureSensor (8) // 1MPa sensor
#define PRESSURE_VALVE_CLOSED_LIMIT (250) // below this pressure valve is assumed closed.
#define HYSTERESIS (1+0.05) // 5% hysteresis either way -- to prevent flapping.

class MachineDeck : public Deck {
public:
    MachineDeck(BlackNodev111 * node) : Deck(node) {};
    void render_pane(bool refresh) {
        _display->clearDisplay();
        _display->print_centred(MACHINE);
        _display->printf("On/Off  : %s\n", powerDetect->state() ? "on" : "off");
        _display->printf("Welding : %s\n", weldingDetect->state() ? "pressed" : "off");
        _display->printf("        : %d [mins]\n", 0.5 + wr.welding_timer/60.);
        _display->printf("Bottle  : %s\n", "unk date");
        
    }
};
class GasDeck : public Deck {
public:
    GasDeck(BlackNodev111 * node) : Deck(node) {};
    void render_pane(bool refresh) {
        float pressure = pressureSensor.readPressureInPa();
        float temperature =pressureSensor.readTemperatureInC();


        _display->clearDisplay();
        _display->print_centred("Gas");
        _display->printf("Press: %.1f [kPa]\n", pressure/1000.):
        _display->printf("Temp : %.1f [C]\n", temperature);
    }
};

void setup() {
    Serial.begin(115200);
    Log.printf("\nBooting(): %s " __DATE__ " " __TIME__ "\n", FILE2FIRMWARE(__FILE__));
    
    expandedPinMode(POWER_GPIO, OUTPUT);
    node.setMonitoredOutput(POWER_GPIO, 0); // relay in the off/safe position
    
    expandedPinMode(VALVE_GPIO, OUTPUT);
    node.setMonitoredOutput(VALVE_GPIO, 0); // value in the off, valve closed
    
    pressureSensor = new XGZP6897D(KpressureSensor);
    if (!pressureSensor.begin())
        Log.println("ERROR pressure sensor not responding");
    
    UNLOCKED = node.machinestate.addState("Switch Welder on", LED::LED_ON,
                                          30 * 1000,  MachineState::WAITINGFORCARD, false);
    WELDING = node.machinestate.addState("Welding", LED::LED_ON,
                                         MachineState::NEVER, MachineState::WAITINGFORCARD, false);
    
    powerDetect = new ButtonDebounce(POWER_VOLTAGE);
    powerDetect->setAnalogThreshold(600);  // typical is 0-50 for off, 1200 for on.
    
    powerDetect->setCallback([](const int newState) {
        if ((node.machinestate == MachineState::CHECKINGCARD || node.machinestate == MachineState::WAITINGFORCARD) && newState == LOW) {
            Log.println("Alert: Power observed while " MACHINE " should be off.");
            node.machinestate = FAULTED;
            power_fault++;
        }
        else if (node.machinestate == UNLOCKED && newState == LOW) {
            Log.println("Machine switched on with front switch");
            node.machinestate = POWERED
            ;
        }
        else if (node.machinestate == FAULTED && newState == HIGH) {
            Log.println("Alert: Odd powerstate cleared.");
            node.machinestate = MachineState::WAITINGFORCARD;
        }
        else if (node.machinestate == POWERED && newState == HIGH) {
            Log.println("Normal poweroff with switch on front.");
            node.machinestate = WAITING_FOR_VALVE;
            normal_poweroff++;
            welding_save();;
        }
        else if (node.machinestate == WELDING && newState == HIGH) {
            Log.println("Machine switched off while welding?!");
            node.machinestate = WAITING_FOR_VALVE;
            bad_poweroff++;
            welding_save();
        }
        else
            Debug.printf("Power now %s (State: %s)\n", newState ? "OFF" : "ON", node.machinestate.label());
    }, CHANGE);
    
    weldingDetect = new ButtonDebounce(WELDING_VOLTAGE);
    weldingDetect->setAnalogThreshold(600);  // typical is 0-50 for off, 1200 for on.
    weldingDetect->setCallback([](const int newState) {
        static unsigned long lst = 0;
        if (node.machinestate == POWERED && newState == LOW) {
            Debug.println("We're welding");
            lst = millis();
            node.machinestate = WELDING;
        } else if (node.machinestate == WELDING && newState == HIGH) {
            Debug.println("Done welding.");
            unsigned long wt = millis() - lst;
            wr.welding_timer = 0.5 + wt/1000.;
            node.machinestate = POWERED;
        } else {
            Log.printf("Alert: Unexpected change in motor current; state is %s and the current is %s\n",
                       node.machinestate.label(), newState ? "ON" : "OFF");
        }
    }, CHANGE);
    
    node.setOTAPasswordHash(ota_password_hash);
    node.set_mqtt_prefix("ac");
    node.set_master("master");
    
    node.setNodeDeck(new MachineDeck(&node));
    
    node.onReport([](JsonObject & report) {
        char * p = __FILE__;
        char * q = rindex(p,'/');
        if (q) p = q;
        report["fw"] = __FILE__ " " __DATE__ " " __TIME__;
        report["power_fault"] = power_fault;
        report["bad_poweroff"] = bad_poweroff;
        report["normal_poweroff"] = normal_poweroff;
        report["idle_poweroff"] = idle_poweroff;
    });
    
    node.setOnChangeCallback(MachineState::WAITINGFORCARD, [&](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
        if ((last == POWERED || last == WELDING)) {
            // Special case for the Welder - we do not want to silently
            // go idle after an hour of non-use; but actually alert people
            // to the fact that this happened.
            //
            node.machinestate = WAITING_FOR_VALVE;
            idle_poweroff++;
        }
        else if (node.machinestate == UNLOCKED) {
            node.updateDisplay("cancel","",true);
        };
    });
    
    node.onApproval([](const char *machine) {
        Log.println("Action Approved.");
        // We allow 'taking over this machine while it is on' -- hence this check for
        // if it is powered; and in that case -also- accepting a new approval.
        //
        if ((node.machinestate != POWERED) &&
            (node.machinestate != MachineState::CHECKINGCARD)
            ) {
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
    
    node.setMenuCallback([&](const int newState) {
        if (node.machinestate == WAITING_FOR_VALVE) {
            Log.println("Valve confirmed closed by button press");
            node.machinestate = MachineState::WAITINGFORCARD;
        }
    },FALLING);
    
    node.setOffCallback([&](const int newState) {
        if (node.machinestate == UNLOCKED) {
            Log.println("Poweron canceled by button press");
            node.machinestate = MachineState::WAITINGFORCARD;
        }
    },FALLING);
    
    welding_init();
    node.begin();
    
    Log.printf("Starting loop(): %s " __DATE__ " " __TIME__ "\n", FILE2FIRMWARE(__FILE__));
}

void loop() {
    node.loop();
    
    if ((node.machinestate == MachineState::WAITINGFORCARD) {
        static unsigned lst = 0;
        if (millis() - lst > 1000) {
            float pressure = pressureSensor.readPressureInPa();
            if (pressure && pressure > PRESSURE_VALVE_CLOSED_LIMIT * HYSTERESIS) {
                Log.println("Detected pressure - assuming problem with the valve.");
                node.machinestate = WAITING_FOR_VALVE;
            };
        };
        lst = millis();
    };
    
    if (node.machinestate == WAITING_FOR_VALVE) {
        float pressure, temperature;
        float pressure = pressureSensor.readPressureInPa();
        if (pressure && pressure < PRESSURE_VALVE_CLOSED_LIMIT/HYSTERESIS) {
            Log.println("Detected pressure drop - assuming valve is closed.");
            node.machinestate = MachineState::WAITINGFORCARD;
        };
        static unsigned lst = 0;
        if (millis() - lst > 1000) {
            lst = millis();
            
            node.updateDisplay("","YES",true);
            
            node.updateDisplayStateMsg("check that both",0);
            node.updateDisplayStateMsg("VALVES are CLOSED ",1);
            
            node.buzzerErr();
        };
    }
    else if (node.machinestate == UNLOCKED) {
        String left = node.machinestate.timeLeftInThisState();
        node.updateDisplayStateMsg("Auto off: " + left, 1);
    };
    
    bool r = (node.machinestate == UNLOCKED) || (node.machinestate == POWERED) || (node.machinestate == WELDING);
    node.setMonitoredOutput(POWER_GPIO, r); // needs to be high to engage the relay
    
    bool v = (node.machinestate == POWERED) || (node.machinestate == WELDING);
    node.setMonitoredOutput(VALVE_GPIO, v); // needs to be high to open the valve
}
