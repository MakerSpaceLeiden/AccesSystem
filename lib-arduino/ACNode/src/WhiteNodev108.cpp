#include "WhiteNodev108.h"
#include "msl-logo.h"
#include <esp_sntp.h>
#include <lwip/ip_addr.h>
#include <qrcode.h> // Part of the ESP32 package

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

Adafruit_SH1106G * _display = NULL;

WhiteNodev108::WhiteNodev108(const char * machine, const char * ssid, const char * ssid_passwd, acnode_proto_t proto) :
ACNode(machine,ssid,ssid_passwd,proto) {
    pop();
};

WhiteNodev108::WhiteNodev108(const char * machine, bool wired, acnode_proto_t proto) :
ACNode(machine,wired,proto) {
    pop();
};

void WhiteNodev108::pop() {
    Serial.begin(115200);
    // Non standard pins for i2c.
    Wire.begin(I2C_SDA, I2C_SCL);
    
    digitalWrite(BUZZER, LOW);
    pinMode(BUZZER, OUTPUT);
    
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
    
    pinMode(OFF_BUTTON, INPUT_PULLUP);
    pinMode(MENU_BUTTON, INPUT_PULLUP);

    ACNode::pop();
};

void WhiteNodev108::buzzer(bool onOff) {
    digitalWrite(BUZZER, onOff ? HIGH : LOW);
}

// Todo - move to a timer, etc. Or re-use the LED infra.
//
void WhiteNodev108::buzzerOk() {
    digitalWrite(BUZZER, HIGH);
    delay(50);
    digitalWrite(BUZZER, LOW);
};

void WhiteNodev108::buzzerErr() {
    digitalWrite(BUZZER, HIGH);
    delay(50);
    digitalWrite(BUZZER, LOW);
    delay(250);
    digitalWrite(BUZZER, HIGH);
    delay(50);
    digitalWrite(BUZZER, LOW);
};

void WhiteNodev108::setOTAPasswordHash(const char * md5) {
    ArduinoOTA.setPasswordHash(md5);
}

void WhiteNodev108::begin(bool hasScreen) {
    _hasScreen = hasScreen;
    if (_hasScreen && !_display) {
        _display = new Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, SCREEN_RESET);
        _display->setRotation(2); // for purple/white boards - OLED is upside down.
        _display->begin(SCREEN_Address, true);
        _display->clearDisplay();
        _display->drawBitmap(
                             (SCREEN_WIDTH-msl_logo_width)/2,
                             (SCREEN_HEIGHT-msl_logo_height)/2,
                             msl_logo,msl_logo_width,msl_logo_height,SH110X_WHITE);
        _display->display();
    };
    _pageState = PAGE_LAST; // basically the logo
    
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
    
    ACNode::begin(BOARD_NG);

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
        Debug.printf("MENU button %s\n",newState ? "released" : "pressed");
        if (machinestate == SCREENSAVER) {
            machinestate = MachineState::WAITINGFORCARD;
            return;
        };
        if (machinestate == MachineState::WAITINGFORCARD && newState == LOW) {
            Debug.println("Menu press on INFO");
            machinestate = INFODISPLAY;
            return;
        };
        if (machinestate == INFODISPLAY && newState == LOW) {
            if (_pageState+1 == PAGE_LAST)
                machinestate = MachineState::WAITINGFORCARD;
            else
            	updateInfoDisplay((page_t)((int)_pageState+1));
            return;
        };
        if (_menuCallBack &&
            (_menuCallBackMode == CHANGE ||
             (newState && (_menuCallBackMode == ONHIGH || _menuCallBackMode == RISING)) ||
             (!newState &&(_menuCallBackMode == ONLOW || _menuCallBackMode == FALLING))
             ))
            _menuCallBack(newState);
        else
            Debug.println("Right button activity ignored.");
    },  CHANGE);
    
    machinestate.setOnChangeCallback(MachineState::ALL_STATES, [&](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
        Debug.printf("Changing state (%d->%d): %s\n", last, current, machinestate.label());
        errorLed.set(machinestate.ledState());
        
        setDisplayScreensaver(current == SCREENSAVER);
        if (current == FAULTED) {
            updateDisplay("", "", true);
            Debug.println("Machine poweron disabled - machine on/off switch in the 'on' position.");
            errors++;
        } else if (current == MachineState::WAITINGFORCARD) {
            updateDisplay("", "MORE", true);
            if (last == MachineState::CHECKINGCARD)
                buzzerErr();
        } else if (current == MachineState::CHECKINGCARD)
            updateDisplay("", "", true);
        else if (current == INFODISPLAY) {
            updateInfoDisplay();
            return;
        }
        else if (_onChangeCB && (current == _onChangeState || _onChangeState ==MachineState::ALL_STATES))
            _onChangeCB(last, current);
    
        updateDisplayStateMsg(machinestate.label());
    });
    
    if (_reader) _reader->onSwipe([&](const char *tag) -> ACBase::cmd_result_t {
        buzzerOk();
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
 
    updateDisplay("","MORE", true);

  ArduinoOTA.setHostname((_acnode->moi && _acnode->moi[0]) ? _acnode->moi : "unset-acnode");

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

    updateDisplay("","",true);
    updateDisplayStateMsg("updating firmware",0);
    updateDisplayProgressbar(0,true);
    setDisplayScreensaver(false);

    if (strstr(_acnode->moi,"test")) 
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
	    updateDisplayStateMsg("ok, rebooting",1);
	    updateDisplayProgressbar(100);
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
        updateDisplayProgressbar(perc);
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
    updateDisplay("","",true);
    updateDisplayStateMsg("update failed",0);
    updateDisplayStateMsg(cause,1);
    updateDisplayProgressbar(0, true);
    };

    _otaOK = true;
    ArduinoOTA.begin();
  });
  
  ArduinoOTA.begin();
  Debug.println("OTA Enabled");
  _otaOK = true;
}

void WhiteNodev108::setDisplayScreensaver(bool on) {
    if (!_hasScreen) return;
    _display->oled_command(on ? SH110X_DISPLAYOFF : SH110X_DISPLAYON);
}

void WhiteNodev108::updateDisplay( String left, String right, bool rebuildFull) {
    if (!_hasScreen) return;
    if (rebuildFull || _pageState != PAGE_NORMAL) {
        _display->clearDisplay();
        _display->setTextSize(1);
        _display->setTextColor(SH110X_WHITE);
        int i = SCREEN_WIDTH - 6 * strlen(machine);
        _display->setCursor(i>0 ? i/2 : 0, 0);
        _display->println(machine);
        _display->setFont(NULL); // Fairly large 5x7 font
        
        if (left.length() || right.length()) {
            _display->setTextColor(SH110X_BLACK);
            _display->drawFastHLine(0,SCREEN_HEIGHT-8*3-1,SCREEN_WIDTH,SH110X_WHITE);
            _display->drawFastHLine(0,SCREEN_HEIGHT-8*2+3,SCREEN_WIDTH,SH110X_WHITE);
        };
        
        if (left.length()) {
            _display->fillRect(0, SCREEN_HEIGHT-8*3+1, 60, 9, SH110X_WHITE);
            _display->setCursor(1,SCREEN_HEIGHT-8*3+2);
            _display->println(left);
        };
        
        if (right.length()) {
            _display->fillRect(SCREEN_WIDTH-60,  SCREEN_HEIGHT-8*3+1, 60, 9, SH110X_WHITE);
            _display->setCursor(SCREEN_WIDTH-right.length()*6,SCREEN_HEIGHT-8*3+2);
            _display->println(right);
        };
        _pageState = PAGE_NORMAL;
    };
    _display->display();
};

void WhiteNodev108::updateDisplayProgressbar(unsigned int percentage, bool rebuildFull) {
    if (!_hasScreen) return;

    int y = SCREEN_HEIGHT-16;
    int l = (SCREEN_WIDTH-4)*percentage / 100.;

    if (rebuildFull){
       _display->fillRect(0, y, SCREEN_WIDTH, 20, SH110X_BLACK);
       _display->drawRect(0, y, SCREEN_WIDTH, 12, SH110X_WHITE);
     };

    _display->fillRect(0+2, y+2, l, 12-4, SH110X_WHITE);
    _display->display();
}

void WhiteNodev108::updateDisplayStateMsg(String msg, int line) {
    if (!_hasScreen) return;
    int y = 16+line*12;
    _display->fillRect(0, y, SCREEN_WIDTH, 12, SH110X_BLACK);
    int i = SCREEN_WIDTH - 6 * msg.length();
    _display->setCursor(i > 0 ? i/2 : 0, y);
    _display->setTextColor(SH110X_WHITE);
    _display->print(msg);
    
    _display->display();
}

static void _display_centred_title(char * title) {
    int l = (21-strlen(title)-4) /2;
    _display->print(" ");
    for(int i = 0; i < l; i++)
        _display->print("-");
    _display->print(" ");
    _display->print(title);
    _display->print(" ");
    for(int i = 0; i < l; i++)
        _display->print("-");
    _display->print("\n");
};

static void _display_QR(char * title, char * url) {
    esp_qrcode_config_t qrc = {
        .display_func = ([](esp_qrcode_handle_t qrcode){
            int s = esp_qrcode_get_size(qrcode);
            int p = 1;
            while ((s*(p+1) <= SCREEN_WIDTH) && (s*(p+1) <= (SCREEN_HEIGHT))) p++;
            int ox = (SCREEN_WIDTH - p*s)/2;
            // We cannot pass anything to this lambda; as it maps to C, rather than c++.
            // So we use the state of the cursor to dected an empty title.
            //
            int oy = _display->getCursorY() ? (SCREEN_HEIGHT - p*s -1) : (SCREEN_HEIGHT - p*s)/2;
            for (int y = 0; y < s; y++)
                for (int x = 0; x < s; x++)
                    if (p == 1)
                        _display->drawPixel(ox+p*x,oy+p*y, esp_qrcode_get_module(qrcode, x, y) ? SH110X_WHITE : SH110X_BLACK);
                    else
                        _display->fillRect(ox+p*x,oy+p*y,p,p,esp_qrcode_get_module(qrcode, x, y) ? SH110X_WHITE : SH110X_BLACK);
        }),
            .max_qrcode_version = 40,
            .qrcode_ecc_level = 1,
    };
    // Make sure above getCursorY() returns zero if there is no title.
    _display->setCursor(0, 0);
    if (title)
        _display_centred_title(title);
    esp_qrcode_generate(&qrc,url);
    Log.printf("Showing QR with text: <%s>\n", url);
}

void WhiteNodev108::updateInfoDisplay(page_t page) {
    if (!_hasScreen) return;
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
		(s == SNTP_SYNC_STATUS_COMPLETED ? "adjusting" : 
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
            _display->printf("Topic:%s/#\n",mqtt_topic_prefix);
            _display->printf("\n%s/%s/%s/#\n",mqtt_topic_prefix,logpath,moi);
        }
            break;
        case PAGE_QR: {
            char url[128];
            snprintf(url,sizeof(url),QR_URL_REDIRECT_TEMPLATE,moi);
            _display_QR(NULL, url);
        };
            break;
        case PAGE_LOG_QR: {
            char url[32];
            snprintf(url,sizeof(url),"http://%s/",String(localIP().toString()).c_str());
            _display_QR((char *)"view log", url);
        };
            break;
        case PAGE_BUTT:
            if (_pageState != page) {
                _display->println("    -- INPUTS --");
            };
            
            for (int i = 0; i < 6; i++) {
                state_t * s = &states[i];
                int x =  2 + (i / 3)   * SCREEN_WIDTH / 2;
                int y = 16 + (i % 3) *  (SCREEN_HEIGHT - 16) / 3;
                int val;
                
#if 0
                if (s->tpe == INPUT_ANALOG)
                    val = analogRead(s->pin) > 500 ? 1 : 0;
                else
#endif
                    val = !digitalRead(s->pin); // they are all pullup style
                
                if (_pageState != page) {
                    _display->drawRect(x, y, 10, 10, SH110X_WHITE);
                    _display->setCursor(x + 12 , y + 1);
                    _display->print(s->label);
                    // pinMode(s->pin, s->tpe);
                    s->lst = val;
                };
                
                _display->fillRect(x + 2, y + 2, 10 - 4, 10 - 4, val ? SH110X_WHITE : SH110X_BLACK);
                
                if (val != s->lst) {
                    s->lst = val;
                }
            };
            break;
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
    ACNode::loop();
    ArduinoOTA.handle();    

    // This is the ony dynamic page; the others are static once drawn; or
    // are only updated by explicit things like button presses.
    //
    if (_pageState == PAGE_BUTT)
        updateInfoDisplay(PAGE_BUTT);

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
    
    ACNode::report(report);
}
