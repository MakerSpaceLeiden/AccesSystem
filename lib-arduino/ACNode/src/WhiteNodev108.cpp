#include "WhiteNodev108.h"

#include <esp_sntp.h>
#include <lwip/ip_addr.h>

#include "util/cufflink_heartbeat.h"


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
    Serial.begin(115200);
    
    errorLed = new LED(LED_INDICATOR);
    
    // Non standard pins for i2c.
    Wire.begin(I2C_SDA, I2C_SCL);
    
    xdigitalWrite(BUZZER, LOW);
    xpinMode(BUZZER, OUTPUT);
    
    // All nodes have a build-in RFID reader; so fine to hardcode this.
    //
    _reader = new RFID_MFRC522(&Wire, RFID_ADDR, RFID_RESET, RFID_IRQ);
    addHandler(_reader);
    
    FAULTED =     machinestate.addState("Switch Fault", LED::LED_ERROR, MachineState::NEVER, MachineState::NEVER);
    SCREENSAVER = machinestate.addState("Waiting for card, screen dark", LED::LED_OFF, MachineState::NEVER, MachineState::WAITINGFORCARD);
    INFODISPLAY = machinestate.addState("User browsing info pages", LED::LED_OFF, 20 * 1000, MachineState::WAITINGFORCARD);
    POWERED =     machinestate.addState("Powered but idle", LED::LED_ON, MAX_IDLE_TIME * 1000, MachineState::WAITINGFORCARD);
    
    machinestate.setState(MachineState::BOOTING);
    addHandler(&machinestate);
    
    xpinMode(OFF_BUTTON, INPUT_PULLUP);
    xpinMode(MENU_BUTTON, INPUT_PULLUP);
    
    xpinMode(OPTO0, INPUT);
    xpinMode(OPTO1, INPUT);
    
    _pageState = PAGE_LAST; // basically the logo
    
    Serial.println("WhiteNodev108 popped");
    // buzzerErr();
};

void WhiteNodev108::buzzer(bool onOff) {
    xdigitalWrite(BUZZER, onOff ? HIGH : LOW);
}

// Todo - move to a timer, etc. Or re-use the LED infra.
//
void WhiteNodev108::buzzerOk() {
    xdigitalWrite(BUZZER, HIGH);
    delay(50);
    xdigitalWrite(BUZZER, LOW);
};

void WhiteNodev108::buzzerErr() {
    xdigitalWrite(BUZZER, HIGH);
    delay(50);
    xdigitalWrite(BUZZER, LOW);
    delay(250);
    xdigitalWrite(BUZZER, HIGH);
    delay(50);
    xdigitalWrite(BUZZER, LOW);
};

void WhiteNodev108::setOTAPasswordHash(const char * md5) {
    ArduinoOTA.setPasswordHash(md5);
}

void WhiteNodev108::begin() {
    Serial.println("WhiteNodev108 begin");

    if (!_display && (_display = new Display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, SCREEN_RESET))) {
        _display->setRotation(2); // for purple/white boards - OLED is upside down.
        _display->begin(SCREEN_Address, true);
        _hasScreen = true;
        Log.println("LCD/OLED screen found and initialized.");
    } else {
        Log.println("No LCD/OLED screen found");
        _hasScreen = false;
    };
    
    if (_wired)
        ETH.begin(WN_ETH_PHY_ADDR, WN_ETH_PHY_POWER, WN_ETH_PHY_MDC, WN_ETH_PHY_MDIO, WN_ETH_PHY_TYPE, WN_ETH_CLK_MODE);
    
#if 0
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_POOL);
    esp_netif_sntp_init(&config);
#else
    configTime(0, 0, NTP_POOL);
    setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",0);
    tzset();
#endif
    esp_sntp_servermode_dhcp(true);

    super::begin(BOARD_NG);
    
    offButton = new ButtonDebounce(OFF_BUTTON);
    offButton->setCallback([&](const int newState) {
        Debug.printf("OFF button %s\n",newState ? "released" : "pressed");
        if (machinestate == SCREENSAVER) {
            machinestate = MachineState::WAITINGFORCARD;
            return;
        };
        if (machinestate == INFODISPLAY && newState == LOW) {
            Debug.println("Exiting INFO by button press");
            machinestate = MachineState::WAITINGFORCARD;
            return;
        };
        if (_offCallBack &&
            (_offCallBackMode == CHANGE ||
             (newState && (_offCallBackMode == ONHIGH || _offCallBackMode == RISING)) ||
             (!newState &&(_offCallBackMode == ONLOW || _offCallBackMode == FALLING))
             ))
            _offCallBack(newState);
        else
            Debug.println("Left button activity ignored.");
    },  CHANGE);
    
    menuButton = new ButtonDebounce(MENU_BUTTON);
    menuButton->setCallback([&](const int newState) {
        Debug.printf("MENU button %s @ %s\n",newState ? "released" : "pressed", machinestate.label());

        if (machinestate == SCREENSAVER) {
            machinestate = MachineState::WAITINGFORCARD;
            return;
        };
        if (machinestate == MachineState::WAITINGFORCARD && newState == LOW) {
            Debug.println("Menu press on INFO\n");
            machinestate = INFODISPLAY;
            return;
        };
        if (machinestate == INFODISPLAY && newState == LOW) {
            if (_pageState+1 == PAGE_LAST) {
                machinestate = MachineState::WAITINGFORCARD;
                Debug.printf("At last page.\n");
            } else {
                updateInfoDisplay((page_t)((int)_pageState+1));
                Debug.printf("Goto next page (%d)\n", _pageState);
            };
            return;
        };
        if (_menuCallBack &&
            (_menuCallBackMode == CHANGE ||
             (newState && (_menuCallBackMode == ONHIGH || _menuCallBackMode == RISING)) ||
             (!newState &&(_menuCallBackMode == ONLOW || _menuCallBackMode == FALLING))
             ))
            _menuCallBack(newState);
        else
            Debug.println("MENU button activity ignored.");
    },  CHANGE);
    
    machinestate.setOnChangeCallback(MachineState::ALL_STATES, [&](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
        Debug.printf("Changing state (%d->%d): %s\n", last, current, machinestate.label());
        errorLed->set(machinestate.ledState());
        
        setDisplayScreensaver(current == SCREENSAVER);
        if (current == FAULTED) {
            _display->updateDisplay(machine, "", "", true);
            Debug.println("Machine poweron disabled - machine on/off switch in the 'on' position.");
            errors++;
        } else if (current == MachineState::WAITINGFORCARD) {
            _display->updateDisplay(machine, "", "MORE", true);
            if (last == MachineState::CHECKINGCARD)
                buzzerErr();
        } else if (current == MachineState::CHECKINGCARD)
            _display->updateDisplay(machine, "", "", true);
        else if (current == INFODISPLAY) {
            updateInfoDisplay();
            return;
        }
        else if (_onChangeCB && (current == _onChangeState || _onChangeState ==MachineState::ALL_STATES))
            _onChangeCB(last, current);
        
        _display->updateDisplayStateMsg(machinestate.label());
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
        machinestate = MachineState::CHECKINGCARD;
        if (_swipeCB)
            return _swipeCB(tag);
        
        return ACBase::CMD_DECLINE;
    });
    
    onConnect([&]() {
        machinestate = MachineState::WAITINGFORCARD;
    });
    onDisconnect([&]() {
        machinestate = MachineState::NOCONN;
    });
    onError([&](acnode_error_t err) {
        Log.printf("Error %d\n", err);
        machinestate = MachineState::TRANSIENTERROR;
    });
    
    onDenied([&](const char *machine) {
        if (machinestate == SCREENSAVER) {
            machinestate = MachineState::WAITINGFORCARD;
            return;
        };
        machinestate = MachineState::REJECTED;
        buzzerErr();
    });
    
    _display->updateDisplay(machine, "","MORE", true);
    
    ArduinoOTA.setHostname((_acnodebase->moi && _acnodebase->moi[0]) ? _acnodebase->moi : "unset-acnode");
    
    ArduinoOTA.onStart([&]() {
        if ((machinestate.state() != MachineState::WAITINGFORCARD) && (machinestate.state() != SCREENSAVER)) {
            Log.printf("CRITICAL: Rejected OTA updated as machine is currently in use (state %d:%s)\n",
                       machinestate.state(), machinestate.label());
            
            // Until our pull requests makes it through - there appears to be no reliable
            // way to abort an OTA upload forcefully. An .end() gets reset by the next
            // valid UDP packet arriving. So in onProgress we keep messing with the state
            // until it positively errors out.
            _otaOK = false;
            for(int i = 0; i < 100; i++) { ArduinoOTA.end(); delay(20); };
            return;
        };
        
        _display->updateDisplay(machine, "","",true);
        _display->updateDisplayStateMsg("updating firmware",0);
        _display->updateDisplayProgressbar(0,true);
        _display->setDisplayScreensaver(false);
        
        if (strstr(_acnodebase->moi,"test"))
            Log.println("OTA process started (Not wiping private keys in test ode).");
        else {
            Log.println("OTA process started -- wiping private keys.");
            wipe_eeprom();
            Log.println("Keys wiped. Do not forget to reset the TOFU on the server.");
        };
        Serial.print("Progress: 0%");
        // Log.stop();
        // Debug.stop();
    });
    ArduinoOTA.onEnd([&]() {
        if (_otaOK) {
            _display->updateDisplayStateMsg("ok, rebooting",1);
            _display->updateDisplayProgressbar(100);
            Serial.println("..100% Done");
            Log.println("OTA process completed, rebooting");
        } else {
            Log.println("Ignoring an OTA end (as we are trying to reject the OTA");
        };
        _otaOK = true;
    });
    ArduinoOTA.onProgress([&](unsigned int progress, unsigned int total) {
        if (!_otaOK) {
            Log.println("Ignoring OTA update; trying to block it.");
            for(int i = 0; i < 100; i++) { ArduinoOTA.end(); delay(20); };
            return;
        };
        static int lp = 0;
        int p = (int)(30. * progress / total + 0.5);
        if (p != lp) {
            lp = p;
            int perc = (progress / (total / 100));
            Serial.printf("..%u%%", perc);
            _display->updateDisplayProgressbar(perc);
        };
    });
    ArduinoOTA.onError([&](ota_error_t error) {
        String cause = String(error);
        
        if (error == OTA_AUTH_ERROR) cause = "OTA: Auth failed";
        else if (error == OTA_BEGIN_ERROR) cause = "OTA: Begin failed";
        else if (error == OTA_CONNECT_ERROR) cause = "OTA: Connect failed";
        else if (error == OTA_RECEIVE_ERROR) cause = "OTA: Receive failed";
        else if (error == OTA_END_ERROR) cause = "OTA: End failed";
        
        Log.println("OTA Failed: " + cause);
        
        // If we did not reject the upload; then do not
        // change state; to prevent us messing with the
        // current machine state or the display.
        //
        if (_otaOK) {
            machinestate = MachineState::TRANSIENTERROR;
            _display->updateDisplay(machine, "","",true);
            _display->updateDisplayStateMsg("update failed",0);
            _display->updateDisplayStateMsg(cause,1);
            _display->updateDisplayProgressbar(0, true);
        };
        
        _otaOK = true;
        ArduinoOTA.begin();
    });
    
    ArduinoOTA.begin();
    Debug.println("OTA Enabled");
    _otaOK = true;
    // buzzerOk();
}

void WhiteNodev108::updateInfoDisplay(page_t page) {
    if (!_hasScreen)
        return;
    if (_pageState != page || page == PAGE_SNTP) {
        _display->clearDisplay();
        _display->setTextSize(1);
        _display->setTextColor(SH110X_WHITE);
        _display->setCursor(0, 0);
    };
    _display->setFont(NULL); // Fairly large 5x7 font
    switch(page) {
        case PAGE_INFO:
            _display->println("   -- INFO --");
            _display->printf("Node :%s\n",moi);
            _display->printf("IPv4 :%s\n", String(localIP().toString()).c_str());
            _display->printf("Via  :%s\n", _wired ? "LAN" : "WiFi");
#ifdef SYSLOG_HOST
            _display->printf("Syslg:%s\n", SYSLOG_HOST);
#else
            _display->printf("Syslg:OFF\n");
#endif
            _display->printf("Up   :%s\n",uptime().c_str());
            _display->printf("CPU  :%.1f%cC\n", coreTemp(),ADAFRUIT_GFX_DEGREE_SYMBOL);
            _display->printf("Heap :%.1fkB\n", ESP.getFreeHeap() / 1024.);
            break;
        case PAGE_SNTP:
        {
            time_t now = time(NULL);
            struct tm * t = localtime(&now);
            char ds[10], ts[10];
            strftime(ds,sizeof(ds),"%Y-%m-%d",t);
            strftime(ts,sizeof(ts),"%H:%M:%S",t);
            sntp_sync_status_t  s = sntp_get_sync_status();
            _display->println("   -- SNTP --");
            _display->printf("Date :%s\n",ds);
            _display->printf("Time :%s\n",ts);
            _display->printf("sNTP :%s\n",esp_sntp_enabled() ?
                             (s == SNTP_SYNC_STATUS_IN_PROGRESS ? "adjusting" :
                              (s == SNTP_SYNC_STATUS_COMPLETED ? "OK" : "Pending")
                              ) : "OFF");
            for(int i = 0, j = 0; i < SNTP_MAX_SERVERS&& j < 5; i++) {
                char buff[INET6_ADDRSTRLEN];
                const char * s = esp_sntp_getservername(i);
                if (!s) {
                    ip_addr_t const *ip = esp_sntp_getserver(i);
                    if (ipaddr_ntoa_r(ip, buff, INET6_ADDRSTRLEN) != NULL && !(ip_addr_isany(ip)))
                        s = buff;
                };
                if (s) {
                    _display->printf("     :%s\n",s);
                    j++;
                };
            };
        };
            break;
        case PAGE_FW:
            _display->println(" -- Firmware --");
            _display->printf("Dev :%s\n", name());
            _display->printf("Date:%s\n",__DATE__);
            _display->printf("Time:%s\n",__TIME__);
            break;
        case PAGE_MQTT: {
            _display->println("    -- MQTT --");
            char buff[16],*p = mqtt_server,*q=(char*)"Host";
            while(*p) {
                char * s = index(p,'.');
                int l = 12;
                if (s && s - p < l && strlen(p) > l) l = s - p+1;
                strncpy(buff,p,l);
                buff[l] = '\0';
                p+=strlen(buff);
                _display->printf("%s :%s\n",q,buff);
                q = (char *)"    ";
            };
            _display->printf("Port :%u\n",mqtt_port);
            _display->printf("Topic:%s/%s\n",mqtt_topic_prefix,logpath);
            _display->printf(" /%s/#\n",moi);
        }
            break;
        case PAGE_QR: {
            char url[128];
            snprintf(url,sizeof(url),QR_URL_REDIRECT_TEMPLATE,moi);
            _display->print_centered_QR(NULL, url);
        };
            break;
        case PAGE_LOG_QR: {
            char url[32];
            snprintf(url,sizeof(url),"http://%s/",String(localIP().toString()).c_str());
            _display->print_centered_QR((char *)"view log", url);
        };
            break;
        case PAGE_BUTT:
            if (_pageState != page) {
                _display->println("    -- INPUTS --");
            };
#if 0
            for (int i = 0;; i++) {
                iostate_t * s = &iostates[i];
                if (!s->label)
                    break;
                
                int x =  2 + (i / 4)   * SCREEN_WIDTH / 2;
                int y = 12 + (i % 4) * 11;
                
                // first time round - print the text and UI; after that
                // just deal with the updates.
                if (_pageState != page) {
                    _display->drawRect(x, y, 10, 10, SH110X_WHITE);
                    _display->setCursor(x + 12 , y + 1);
                    _display->print(s->label);
                };
                
                // upate the on/off dot in the middle always.
                s->lst = !xdigitalRead(s->pin); // they are all pullup style
                _display->fillRect(x + 2, y + 2, 10 - 4, 10 - 4, s->lst ? SH110X_WHITE : SH110X_BLACK);
            }
#endif
            break;
        case PAGE_LED:
            _display->println("-- LED showtime --");
            // this will go wrong - LEDs will stay on XXX
        {
            bool onOff = (millis()>>10) & 1;
            for(const uint8_t * p = leds(); *p != 255; p++)
                xdigitalWrite(*p, onOff);
        }
            break;
        case PAGE_LAST:
        default:
            _display->println("Bug - page not defined");
            break;
    };
    _display->display();
    _pageState = page;
}

void WhiteNodev108::onSwipe(RFID::THandlerFunction_SwipeCB swipeCB) {
    _swipeCB = swipeCB;
};

void WhiteNodev108::loop() {
    super::loop();
    ArduinoOTA.handle();
    
    // Some pages are dynamic; and need to be updated
    // constantly.
    //
    if (_pageState == PAGE_BUTT || _pageState == PAGE_LED)
        updateInfoDisplay(_pageState);
    
    if (_pageState == PAGE_SNTP)
        updateInfoDisplay(PAGE_SNTP);
    
    if (machinestate == POWERED) {
        String left = machinestate.timeLeftInThisState();
        // Show the countdown to poweroff; only when the machine
        // has been idle for a singificant bit of time.
        //
        if (left.length() && machinestate.secondsInThisState() > SHOW_COUNTDOWN_TIME_AFTER)
            updateDisplayStateMsg("Auto off: " + left, 1);
    };
    
    if ((machinestate == MachineState::WAITINGFORCARD) && (machinestate.secondsInThisState() > SCREENSAVER_DELAY)) {
        Debug.println("Enabling screensaver");
        machinestate.setState(SCREENSAVER);
    };
}

void WhiteNodev108::report(JsonObject & report) {
    report["manual_poweroff"] = manual_poweroff;
    report["idle_poweroff"] = idle_poweroff;
    report["errors"] = errors;
    
    report["ota"] = true;
    
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

BlackNodev111::BlackNodev111(const char * machine, const char * ssid, const char * ssid_passwd, acnode_proto_t proto)
: WhiteNodev108(machine, ssid, ssid_passwd, proto)  { CONSTS(); pop(); };

BlackNodev111::BlackNodev111(const char * machine, bool wired, acnode_proto_t proto )
: WhiteNodev108(machine,wired,proto) { CONSTS(); pop(); };

void BlackNodev111::setMonitoredOutput(uint8_t num, bool val) {
    if (num == OUT0)
        expectOut1 = val ? HIGH : LOW;
    if (num == OUT1)
        expectOut2 = val ? HIGH : LOW;
    xdigitalWrite(num,val);
}

void BlackNodev111::pop() {
    Serial.println("BlackNodev11 popped");
    yield();
};

void BlackNodev111::begin() {
    Serial.println("BlackNodev11 begin.");
    
    // Starting with v1.11 - non core I/O is provided by an i2c IO/Expander.
    //
    ExpandedGPIO::getInstance().addAW9523();
    
    // Reduce the current to a sensible level (Awaiting https://github.com/adafruit/Adafruit_AW9523/pull/5)
    //
    Wire.beginTransmission(0x58);
    Wire.write(0x11);
    Wire.write(3);
    Wire.endTransmission();
    
    xpinMode(LEDA,AW9523_LED_MODE);
    xanalogWrite(LEDA,0);
    
    xpinMode(LEDB,AW9523_LED_MODE);
    xanalogWrite(LEDB,0);
    
    xpinMode(LEDC,AW9523_LED_MODE);
    xanalogWrite(LEDC,0);
    
    xpinMode(LEDD,AW9523_LED_MODE);
    xanalogWrite(LEDD,0);
    
    xpinMode(LEDE,AW9523_LED_MODE);
    xanalogWrite(LEDE,0);
    
    // Reduce the current to a sensible level (Awaiting https://github.com/adafruit/Adafruit_AW9523/pull/5)
    Wire.beginTransmission(0x58);
    Wire.write(0x11);
    Wire.write(3);
    Wire.endTransmission();
    
    xpinMode(OPTO0, INPUT);
    xpinMode(OPTO1, INPUT);
    xpinMode(OPTO2, INPUT);
    xpinMode(OPTO3, INPUT);
    
    Serial.println("BlackNodev11 began");
    super::begin();
}

void BlackNodev111::loop() {
    xanalogWrite(LEDE,hearthbeat());
    
    super::loop();
    
    // Quite hardware specific; the relay can only be forced 'on' - either by a GPIO or
    // by a switch. It cannot be forced off. So we can only sensibly detect an 'illegal' on;
    // while it was expected to be off.
    //
    if (expectOut1 == LOW) {
        static unsigned long lst = 0;
        xpinMode(OUT0,INPUT);
        if ( xdigitalRead(OUT0) != expectOut1) {
            if (lst == 0 || millis() - lst > 5 * 60 * 1000) {
                Log.printf("Warning - Output 1 measured as %s at hardware level; it should be %s.\n",
                           xdigitalRead(OUT0) ? "HIGH" : "LOW", expectOut1  ? "HIGH" : "LOW");
                lst = millis();
            };
            errorLed->set(LED::LED_FAST);
            xanalogWrite(LEDA,255);
        } else lst = 0;
        xpinMode(OUT0,OUTPUT);
    };
    
    if (expectOut2 == LOW) {
        static unsigned long lst = 0;
        xpinMode(OUT1,INPUT);
        if (digitalRead(OUT1) != expectOut2) {
            if (lst == 0 || millis() - lst > 5 * 60 * 1000) {
                Log.printf("Warning - Output 2 measured as %s at hardware level; it should be %s.\n",
                           xdigitalRead(OUT1) ? "HIGH" : "LOW", expectOut2  ? "HIGH" : "LOW");
                lst = millis();
            };
            errorLed->set(LED::LED_FAST);
            xanalogWrite(LEDA,255);
        } else lst = 0;
        xpinMode(OUT1,OUTPUT);
    };
}
