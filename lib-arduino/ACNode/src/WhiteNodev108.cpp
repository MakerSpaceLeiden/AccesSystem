#include "WhiteNodev108.h"

#include <esp_sntp.h>
#include <lwip/ip_addr.h>
#include <esp_debug_helpers.h>
#include <esp_task_wdt.h>
#include <hal/wdt_hal.h>
#include <hal/wdt_types.h>


#include "Display/Deck.h"
#include "OTA.h"

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN (48)
#endif

#define WN_ETH_PHY_TYPE        ETH_PHY_RTL8201
#define WN_ETH_PHY_ADDR         0 // PHYADxx all tied to 0
#define WN_ETH_PHY_MDC         23
#define WN_ETH_PHY_MDIO        18
#define WN_ETH_CLK_MODE        ETH_CLOCK_GPIO17_OUT

#define WN_ETH_PHY_POWER       -1 // powersafe in software
#define WN_ETH_PHY_RESET       -1 // wired to EN/esp32 reset

#include <ETH.h>
#include <WiredEthernet.h>

#ifndef ADAFRUIT_GFX_DEGREE_SYMBOL
#define ADAFRUIT_GFX_DEGREE_SYMBOL (247)
#endif

#define QR_URL_REDIRECT_TEMPLATE "https://wiki.makerspaceleiden.nl/mediawiki/index.php/QR_%s"

// Extra, hardware specific states
MachineState::machinestate_t FAULTED, SCREENSAVER, INFODISPLAY, POWERED;

Display * _display = NULL;

WhiteNodev108::WhiteNodev108(const char * machine, const char * ssid, const char * ssid_passwd, acnode_proto_t proto) :
super(machine,ssid,ssid_passwd)
{
    CONSTS();
    pop();
};

WhiteNodev108::WhiteNodev108(const char * machine, bool wired, acnode_proto_t proto) :
super(machine,wired)
{
    CONSTS();
    pop();
};

void WhiteNodev108::pop() {
    // Non standard pins for i2c.
    Wire.begin(I2C_SDA, I2C_SCL);
    
    buzzer(false);;
    xpinMode(BUZZER, OUTPUT);
    
    FAULTED =     machinestate.addState("Switch Fault", LED::LED_ERROR, MachineState::NEVER, MachineState::NEVER);
    SCREENSAVER = machinestate.addState("Waiting for card, screen dark", LED::LED_OFF, MachineState::NEVER, MachineState::WAITINGFORCARD);
    INFODISPLAY = machinestate.addState("User browsing info pages", LED::LED_OFF, 20 * 1000, MachineState::WAITINGFORCARD);
    POWERED =     machinestate.addState("Powered but idle", LED::LED_ON, MAX_IDLE_TIME * 1000, MachineState::WAITINGFORCARD);
    
    xpinMode(OFF_BUTTON, INPUT_PULLUP);
    xpinMode(MENU_BUTTON, INPUT_PULLUP);

    xpinMode(OPTO0, INPUT);
    xpinMode(OPTO1, INPUT);
    
    if (!errorLed)
        errorLed = new LED(LED_INDICATOR);

    _deskCtrl = new DeckController();
    addHandler(_deskCtrl);
};

// bracketing with a timer to keep some cadence. We should
// moved to timer/interrupt async queue.
//
#define BUZZ_MIN_INTERVAL (50)

void WhiteNodev108::buzzer(bool onOff) {
    while(onOff && ((millis() - _last_buzz) < BUZZ_MIN_INTERVAL)) { delay(1); };
    xdigitalWrite(BUZZER, onOff ? HIGH : LOW);
    if (!onOff)
        _last_buzz = millis();
}

// Todo - move to a timer, etc. Or re-use the LED infra.
//
void WhiteNodev108::buzzerOk() {
    buzzer(true);
    delay(BUZZ_MIN_INTERVAL);
    buzzer(false);
};

void WhiteNodev108::buzzerErr() {
    buzzerOk();
    delay(BUZZ_MIN_INTERVAL*5);
    buzzerOk();
};

void WhiteNodev108::begin() {
    errorLed->begin();

    // All nodes have a build-in RFID reader; so fine to hardcode this.
    //
    _reader = new RFID_MFRC522(&Wire, RFID_ADDR, RFID_RESET, RFID_IRQ);
    addHandler(_reader);

    if (!_display && (_display = new Display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, SCREEN_RESET))) {
        _display->setRotation(2); // for purple/white boards - OLED is upside down.
        _display->begin(SCREEN_Address, true, strstr(machine,"test") ? (const char*)__TIME__ : (const char*)"");
        Log.println("LCD/OLED screen found and initialized.");
    } else {
        Log.println("No LCD/OLED screen found");
    };

    OTAWithDisplay * ota = new OTAWithDisplay(OTA_PASSWD_HASH, _display, moi);
    ota->setOTAOK([&](){
        return machinestate.safeForOTA();
    });
    ota->setPreOTASecretWiper([](){
        Log.println("*** NOT IMPLEMENTED ***");
    });
    addHandler(ota);

    _deskCtrl->addDeck( new QrDeck(this, machine));
    _deskCtrl->addDeck( new InfoDeck(this));

    approvalDeck = new ApprovalDeck(this, _approvalAPI);
    _deskCtrl->addDeck( approvalDeck );

    _deskCtrl->addDeck( new LogQrDeck(this));
    _deskCtrl->addDeck( new SNTPDeck(this));

    firmwareDeck =  new FirmwareDeck(this);
    _deskCtrl->addDeck( firmwareDeck);

    _deskCtrl->addDeck( new RfidDeck(this, _reader));
    _deskCtrl->addDeck( new OTADeck(this,ota));
    _deskCtrl->addDeck( new MqttDeck(this));
    _deskCtrl->addDeck( new RestDeck(this, _restAPI));
    
    if (strstr(machine,"test"))
        _deskCtrl->addDeck(new ButtonsDeck(this, iostates));
   
    if (_wired)
#if ESP_ARDUINO_VERSION_MAJOR == 2
        ETH.begin(WN_ETH_PHY_ADDR, WN_ETH_PHY_POWER, WN_ETH_PHY_MDC, WN_ETH_PHY_MDIO, WN_ETH_PHY_TYPE, WN_ETH_CLK_MODE);
#else
	// 3.x version - signature changes
	ETH.begin(WN_ETH_PHY_TYPE, WN_ETH_PHY_ADDR, WN_ETH_PHY_MDC, WN_ETH_PHY_MDIO, WN_ETH_PHY_POWER, WN_ETH_CLK_MODE);
#endif
    
#if 0
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_POOL);
    esp_netif_sntp_init(&config);
#if 0 // ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(3, 0, 0)
    esp_sntp_servermode_dhcp(true);
#endif
#else
    configTime(0, 0, NTP_POOL);
    setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",0);
    tzset();
#endif
    
    offButton = new IODebounce(OFF_BUTTON);
    offButton->setCallback([&](const int newState) {
        Debug.printf("OFF button %s\n",newState ? "released" : "pressed");

        if (_offCallBack &&
            (_offCallBackMode == CHANGE ||
             (newState && (_offCallBackMode == ONHIGH || _offCallBackMode == RISING)) ||
             (!newState &&(_offCallBackMode == ONLOW || _offCallBackMode == FALLING))
             ))
            if (_offCallBack(newState))
                return;

        if (machinestate == SCREENSAVER) {
            machinestate = MachineState::WAITINGFORCARD;
            Debug.println("Switching off the screensaver");
            return;
        } else
        if (machinestate == INFODISPLAY && newState == LOW) {
            if (currentDeck() == approvalDeck) {
                Log.println("Forcing an immediate update on button press");
                _approvalAPI->scheduleImmediateUpdate();
                return;
            };
            if (currentDeck() == firmwareDeck) {
                Log.println("Forcing an immediate reboot on button press");
		machinestate = MachineState::REBOOT;
                return;
	    };
            
            Debug.println("Exiting INFO by button press");
            machinestate = MachineState::WAITINGFORCARD;
            _deskCtrl->close();
            return;
        };
    },  CHANGE);
    addHandler(offButton);
  
    pinMode(14,INPUT_PULLUP);
    menuButton = new IODebounce(MENU_BUTTON);
    menuButton->setCallback([&](const int newState) {
        Debug.printf("MENU button %s @ %s\n",newState ? "released" : "pressed", machinestate.label());
        if (_menuCallBack &&
            (_menuCallBackMode == CHANGE ||
             (newState && (_menuCallBackMode == ONHIGH || _menuCallBackMode == RISING)) ||
             (!newState &&(_menuCallBackMode == ONLOW || _menuCallBackMode == FALLING))
             ))
            if (_menuCallBack(newState))
                return;

        if (machinestate == SCREENSAVER) {
            machinestate = MachineState::WAITINGFORCARD;
            Debug.println("Switching off the screensaver");
            return;
        };
        if (machinestate.safeForOTA() /*  MachineState::WAITINGFORCARD */ && newState == LOW && machinestate != INFODISPLAY) {
            Debug.println("Menu press on INFO");
            machinestate = INFODISPLAY;
            return;
        };
        if (machinestate == INFODISPLAY && newState == LOW) {
            if (!_deskCtrl->next()) {
                _deskCtrl->close();
                machinestate = MachineState::WAITINGFORCARD;
                Debug.println("At last page");
            } else {
                machinestate.resetTimeout();
                Debug.println("Next page");
            }
            return;
        };
    },  CHANGE);
    addHandler(menuButton);
   
    machinestate.setOnChangeCallback(MachineState::ALL_STATES, [&](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
        Debug.printf("WhiteNodev108: Changing state (%d->%d): %s\n", last, current, machinestate.label());

        errorLed->set(machinestate.ledState());

        if (last == INFODISPLAY)
            _deskCtrl->close();
            
        _display->clearDisplay();
        _display->setDisplayScreensaver(current == SCREENSAVER);

        if (current == FAULTED) {
            updateDisplay("", "", true);
            Debug.println("Machine poweron disabled - machine on/off switch in the 'on' position.");
            errors++;
        } else if (current == MachineState::WAITINGFORCARD) {
            updateDisplay("", "MORE", true);
            if (last == MachineState::CHECKINGCARD)
                buzzerErr();
        } else if (current == MachineState::CHECKINGCARD) {
            updateDisplay("", "", true);
        } else if (current == INFODISPLAY) {
            _deskCtrl->first();
            return;
        } else if (current == MachineState::REJECTED) {
            _display->updateDisplayStateMsg(_lasterrmsg,1);
            buzzerErr();
        };
        
        if (current != INFODISPLAY)
            updateDisplayStateMsg(machinestate.label());

        if (_onChangeCB && (current == _onChangeState || _onChangeState ==MachineState::ALL_STATES))
            _onChangeCB(last, current);
    });
    
    if (_reader) _reader->onSwipe([&](const char *tag) -> ACBase::cmd_result_t {
        buzzerOk();
        
        ACBase::cmd_result_t ret;
        if ((ret=_restAPI->handleTagSwipe(tag)) != ACBase::CMD_DECLINE)
            return ret;
        
        if (machinestate < MachineState::WAITINGFORCARD) {
            Log.printf("Ignoring swipe; as the node is not yet ready for it\n");
            return ACBase::CMD_CLAIMED;
        };
        if (machinestate == SCREENSAVER) {
            machinestate.setState(MachineState::WAITINGFORCARD);
            Debug.println("Switching off the screensaver");
        };
        if (machinestate == INFODISPLAY) {
            machinestate.setState(MachineState::WAITINGFORCARD);
            Debug.println("Aborting INFO screen to handle swipe");
        };
        
        // Only go through the checking card rigamarole if we
        // are in a normal check. For the specials, such aws
        // when adding instructions or confirming maintenance,
        // we stay in the actual state.
        //
        if (machinestate == MachineState::WAITINGFORCARD) {
            machinestate = MachineState::CHECKINGCARD;
            updateDisplay("", "", true);
            updateDisplayStateMsg(machinestate.label());
        };

        if (_swipeCB)
            return _swipeCB(tag);
     
        return ACBase::CMD_DECLINE;
    });
    
    onConnect([&]() {
        machinestate = PAIRING;
    });
    onDisconnect([&]() {
        machinestate = MachineState::NOCONN;
    });
    onError([&](acnode_error_t err) {
        Log.printf("Error %d\n", err);
        machinestate = MachineState::TRANSIENTERROR;
    });
    
    onDenied([&](const char *reason) {
        _lasterrmsg = reason;
        machinestate = MachineState::REJECTED;
    });
    
    updateDisplay("","MORE", true);
    super::begin(BOARD_NG);
}

void WhiteNodev108::updateDisplay(String left, String right, bool rebuildFull) {
    // Debug.printf("WhiteNodev108::updateDisplay %d\n", rebuildFull);
    _display->updateDisplay(machine,left,right,rebuildFull);
};

void WhiteNodev108::updateDisplayStateMsg(String msg,int line) {
    // Debug.printf("Updating display: %d:%s\n",line,msg.c_str());
    _display->updateDisplayStateMsg(msg, line);
}

void WhiteNodev108::onSwipe(RFID::THandlerFunction_SwipeCB swipeCB) {
    _swipeCB = swipeCB;
};

void WhiteNodev108::loop() {    
    if (machinestate == POWERED) {
        // Show the countdown to poweroff; only when the machine
        // has been idle for a singificant bit of time and in the
        // last 30 seconds (the latter when the TO is short for
        // testing purposes).
        //
        if (
            (machinestate.secondsLeftInThisState() < 45) ||
            (machinestate.secondsInThisState() > SHOW_COUNTDOWN_TIME_AFTER)
            )
            updateDisplayStateMsg("Auto off in " + machinestate.timeLeftInThisState(), 2);
        
        static unsigned long lst = 0;
        if (machinestate.secondsLeftInThisState() < 30 && millis() - lst > 1000) {
            lst = millis();
            if (machinestate.secondsLeftInThisState() < 2) {
                buzzer(true); delay(1000); buzzer(false);
            } else {
                buzzerOk();
            };
        };
    };
    
    if ((machinestate == MachineState::WAITINGFORCARD) && (machinestate.secondsInThisState() > SCREENSAVER_DELAY)) {
        Debug.println("Enabling screensaver");
        machinestate.setState(SCREENSAVER);
    };
    
    super::loop();
}

void WhiteNodev108::report(JsonObject & report) {
    report["manual_poweroff"] = manual_poweroff;
    report["idle_poweroff"] = idle_poweroff;
    report["errors"] = errors;
    report["ota"] = true;

    report["ntp"] = (bool) esp_sntp_enabled();
    report["ntppool"] = "" NTP_POOL "";
    report["ntpstatus"] = sntp_get_sync_status();

    char buff[27];
    time_t t = time(NULL);
    strncpy(buff,ctime(&t),sizeof(buff));
    buff[24]='\0'; // strip \n
    report["ntpdate"] = buff;

    super::report(report);
}

void WhiteNodev108::setOffCallback(ButtonCallback callback,int mode) {
    _offCallBack = callback;
    _offCallBackMode = mode;
};

void WhiteNodev108::setMenuCallback(ButtonCallback callback, int mode ) {
    _menuCallBack = callback;
    _menuCallBackMode = mode;
}

void WhiteNodev108::setOnChangeCallback(MachineState::machinestate_t state, MachineState::THandlerFunction_OnChangeCB onChangeCB) {
    _onChangeState = state;
    _onChangeCB =onChangeCB;
}

void WhiteNodev108::setNodeDeck(Deck * deck) {
    // Node specific decks, if they exists; are always the first one
    // you see when pressing info/more.
    //
    _deskCtrl->addDeckAsFirst(deck);
}

void WhiteNodev108::addDeck(Deck * deck) {
    _deskCtrl->addDeck(deck);
}

void ButtonsDeck::render_pane(bool refresh) {
    if (refresh)
        _display->print_centred("I/O");
    
    if (!iostates)
        return;

    for (int i = 0;; i++) {
        iostate_t * s = &iostates[i];
        if (!s->label)
            break;
        
        int x =  2 + (i / 4)   * SCREEN_WIDTH / 2;
        int y = 12 + (i % 4) * 11;
        
        // first time round - print the text and UI; after that
        // just deal with the updates.
        if (refresh) {
            _display->drawRect(x, y, 10, 10, SH110X_WHITE);
            _display->setCursor(x + 12 , y + 1);
            _display->print(s->label);
        };
        
        // upate the on/off dot in the middle always.
        s->lst = !(_acnode->xdigitalRead(s->pin)); // they are all pullup style
        _display->fillRect(x + 2, y + 2, 10 - 4, 10 - 4, s->lst ? SH110X_WHITE : SH110X_BLACK);
    }
};

