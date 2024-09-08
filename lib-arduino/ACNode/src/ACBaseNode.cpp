#include <ACNode.h>
#include "ConfigPortal.h"
#include <Cache.h>
#include <EEPROM.h>

#ifdef ESP32
#include <WiFi.h>
#include <ETH.h>
#endif


#include <TelnetSerialStream.h>
TelnetSerialStream telnetSerialStream = TelnetSerialStream();

#include <WebSerialStream.h>
WebSerialStream  webSerialStream = WebSerialStream();

#include <MqttlogStream.h>

#ifdef SYSLOG_HOST
#include <SyslogStream.h>
SyslogStream syslogStream = SyslogStream();
#endif

beat_t beatCounter = 0;      // My own timestamp - manually kept due to SPI timing issues.

float loopRate = 0;

// Unfortunately - MQTT callbacks cannot yet pass
// a pointer. So we need a 'global' variable; and
// sort of treat this class as a singleton. And
// contain this leakage to just a few functions.
//
ACNodeBase *_acnodebase;

void ACNodeBase::set_mqtt_host(const char *p) {
    strncpy(mqtt_server,p, sizeof(mqtt_server));
};
void ACNodeBase::set_mqtt_port(uint16_t p)  { mqtt_port = p; };

void ACNodeBase::set_mqtt_prefix(const char *p)  {
    strncpy(mqtt_topic_prefix,p, sizeof(mqtt_topic_prefix));
}
void ACNodeBase::set_mqtt_log(const char *p)  { strncpy(logpath,p, sizeof(logpath)); };
void ACNodeBase::set_moi(const char *p)  { strncpy(moi,p, sizeof(moi)); };
void ACNodeBase::set_machine(const char *p)  { strncpy(machine,p, sizeof(machine)); };
void ACNodeBase::set_master(const char *p)  { strncpy(master,p, sizeof(master)); };

void ACNodeBase::CONSTS() {
    Serial.begin(115200);
    while(!Serial) { delay(10); };
    Serial.println("\n\n" __DATE__ " - " __TIME__ "\nACNode started");
    
    _acnodebase = this;
};

void ACNodeBase::pop() {

    strncpy(mqtt_server, MQTT_SERVER, sizeof(mqtt_server));
    mqtt_port = MQTT_DEFAULT_PORT;
    _report_period = REPORT_PERIOD;
    
    moi[0] = 0;
    if (machine == NULL || machine[0] == 0)
        strncpy(machine, String("test-" + chipId() ).c_str(), sizeof(machine));
    
    strncpy(mqtt_topic_prefix, MQTT_TOPIC_PREFIX, sizeof(mqtt_topic_prefix));
    strncpy(master, MQTT_TOPIC_MASTER, sizeof(master));
    strncpy(logpath, MQTT_TOPIC_LOG, sizeof(logpath));
    
    Log.setIdentifier(moi);
    Debug.setIdentifier(moi);
    
    // It is safe to start logging early - as these won't emit anyting until
    // the network is known to be up.
    //
    const std::shared_ptr<LOGBase> & th = std::make_shared<TelnetSerialStream>(telnetSerialStream);
    const std::shared_ptr<LOGBase> & wh = std::make_shared<WebSerialStream>(webSerialStream);
    
    Log.addPrintStream(th);
    Log.addPrintStream(wh);
    
    Debug.addPrintStream(wh);
    Debug.addPrintStream(th);
    
#ifdef SYSLOG_HOST
    Log.addPrintStream(std::make_shared<SyslogStream>(syslogStream));
#endif
};

IPAddress ACNodeBase::localIP() {
#ifdef ESP32
    if (_wired)
        return ETH.localIP();
    else
#endif
        return WiFi.localIP();
};

String ACNodeBase::getHostname() {
#ifdef ESP32
    if (_wired)
        return ETH.getHostname();
    else
#endif
        return WiFi.getHostname();
}

String ACNodeBase::macAddressString() {
#ifdef ESP32
    if (_wired)
        return ETH.macAddress();
    else
#endif
        return WiFi.macAddress();
};

ACNodeBase::ACNodeBase(const char * m, bool wired) :
_ssid(NULL), _ssid_passwd(NULL), _wired(wired)
{
    if (m && *m)
        strncpy(machine,m, sizeof(machine));
    CONSTS();
    pop();
}

ACNodeBase::ACNodeBase(const char *m, const char * ssid , const char * ssid_passwd ) :
_ssid(ssid), _ssid_passwd(ssid_passwd), _wired(false)
{
    if (m && *m)
        strncpy(machine,m, sizeof(machine));
    CONSTS();
    pop();
}


void ACNodeBase::set_debugAlive(bool debug) { _debug_alive = debug; }

void ACNodeBase::set_log_destinations(unsigned int destinations) {
    // LOG_SERIAL     LOG_TELNET    LOG_SYSLOG   LOG_WEBBROWSER  LOG_MQTT
}
void ACNodeBase::set_debug_destinations(unsigned int destinations) {
}

bool ACNodeBase::isConnected() {
#ifdef ESP32
    if (_wired)
        return eth_connected();
#endif
    return (WiFi.status() == WL_CONNECTED);
};

void ACNodeBase::addHandler(ACBase * handler) {
    _handlers.insert (_handlers.end(), handler);
}

void ACNodeBase::begin(eth_board_t board /* default is BOARD_AART */, uint8_t clear_button) {
    _begin(board, clear_button);
    _complete_begin(clear_button);
}

void ACNodeBase::_complete_begin(uint8_t clear_button) {
    std::list<ACBase *>::iterator it;
    for (it =_handlers.begin(); it!=_handlers.end(); ++it) {
        (*it)->begin();
    }
    
#if TOFU_WIPE_BUTTON
    // secrit reset button that resets TOFU or the shared
    // secret.
    if (xdigitalRead(TOFU_WIPE_BUTTON) == LOW) {
        extern void wipe_eeprom();
        Log.println("Wiped EEPROM with crypto stuff (SW1 pressed)");
        wipe_eeprom();
    };
#endif
    prepareCache(false);
}

void ACNodeBase::_begin(eth_board_t board /* default is BOARD_AART */, uint8_t clear_button)
{
    if (!*machine)
        strncpy(machine, "unset-machine-name", sizeof(machine));
    
    if (!*moi)
        strncpy(moi, machine, sizeof(moi));
    
    if (strncmp(moi,"test-",5) == 0)
        snprintf(moi,sizeof(moi),"%s-%s",moi,chipId().c_str());
    
#if 0
    if (_debug)
        debugFlash();
#endif
    checkClearEEPromAndCacheButtonPressed(clear_button);
    
#ifdef ESP32
    if (_wired)
        WiFi.onEvent(WiFiEvent);
#endif
    
#ifdef CONFIGAP
    configBegin();
    
    // Go into Config AP mode if the orange button is pressed
    // just post powerup -- or if we have an issue loading the
    // config.
    //
    static int debounce = 0;
    while (xdigitalRead(PUSHBUTTON) == 0 && debounce < 5) {
        debounce++;
        delay(5);
    };
    if (debounce >= 5 || configLoad() == 0)  {
        configPortal();
    }
#endif
    
    if (_wired) {
        Log.println("Wired mode");
        // WiFi.mode(WIFI_STA);
    } else
        if (_ssid) {
            Log.printf("Starting up wifi (hardcoded SSID <%s>)\n", _ssid);
            WiFi.begin(_ssid, _ssid_passwd);
        } else {
            Log.println("Staring wifi auto connect.");
            WiFiManager wifiManager;
            wifiManager.autoConnect();
        };
    
    const int del = 3; // seconds.
    unsigned long start = millis();
    Debug.print("Connecting..");
    while (!isConnected() && (millis() - start < del * 1000)) {
        delay(500);
        Debug.print(".");
    };
    Debug.println("Connected.");
    
    if (!_wired && !isConnected()) {
        // Log.printf("No connection after %d seconds (ssid=%s). Going into config portal (debug mode);.\n", del, WiFi.SSID().c_str());
        // configPortal();
        Log.printf("No connection after %d seconds (ssid=%s). Rebooting.\n", del, WiFi.SSID().c_str());
        Log.println("Rebooting...");
        delay(1000);
        ESP.restart();
    }
    if(_ssid)
        Log.printf("Wifi connected to <%s>\n", WiFi.SSID().c_str());
    
    Log.print(moi); Log.print(" "); Log.println(localIP());
    
    MDNS.begin(moi);
    Log.println("MDNS Responder started");
    
    _espClient = WiFiClient();
    _client = PubSubClient(_espClient);
    
#ifdef CONFIGAP
    configBegin();
#endif
    
    Log.begin();
    Debug.begin();
}


void ACNodeBase::report(JsonObject & out) {
    out[ "node" ] = moi;
    out[ "machine" ] = machine;
    
    out[ "maxMqtt" ] = MAX_MSG;
    
    char chipstr[30]; strncpy(chipstr,chipId().c_str(),sizeof(chipstr));
    out[ "id" ] = chipstr;
    char ipstr[30]; strncpy(ipstr, String(localIP().toString()).c_str(),sizeof(ipstr));
    out[ "ip" ] = ipstr;
    out[ "net" ] = _wired ? "UTP" : "WiFi";
    char macstr[30]; strncpy(macstr, macAddressString().c_str(),sizeof(macstr));
    out[ "mac" ] = macstr;
    
    if (_start_beat == 0)
        if (time(NULL) > 1542275849)
            _start_beat = time(NULL) + millis()/1000;
    
    out[ "approve" ] = _approve;
    out[ "deny" ] = _deny;
    out[ "requests" ] = _reqs;
#ifdef ESP32
    out[ "cache_hit" ] =  cacheHit;
    out[ "cache_miss" ] =  cacheMiss;
    out[ "cache_purge" ] =  cachePurge;
    out[ "cache_update" ] =  cacheUpdate;
#endif
    
    out[ "mqtt_reconnects" ] = _mqtt_reconnects;
    
    out["loop_rate"] = loopRate;
#ifdef ESP32
    out["coreTemp"]  = coreTemp();
#endif
    out["heap_free"] = ESP.getFreeHeap();
    
    std::list<ACBase *>::iterator it;
    for (it =_handlers.begin(); it!=_handlers.end(); ++it)
        (*it)->report(out);
    
    if (_report_callback)
        _report_callback(out);
}

void ACNodeBase::loop() {
    {
        static unsigned long last = 0, lastCntr = 0, Cntr = 0;
        Cntr++;
        if (millis() - last > (unsigned long)(30 * 1000)) {
            float rate =  1000. * (Cntr - lastCntr)/(millis() - last) + 0.05;
            loopRate = rate;
            if (rate > 10)
                Debug.printf("Loop rate: %.1f #/second\n", rate);
            else
                Log.printf("Warning: LOW Loop rate: %.1f #/second\n", rate);
            last = millis();
            lastCntr = Cntr;
        }
    }
    
#if 0
    if (_debug) {
        static unsigned long last = millis();
        static unsigned long sw1, sw2, tock;
        sw1  += xdigitalRead(SW1_BUTTON);
        sw2  += xdigitalRead(SW1_BUTTON);
        tock ++;
        if (millis() - last > 1000) {
            Debug.printf("SW1: %d %d SW2: %d %d Relay %d Triac %d\n",
                         xdigitalRead(SW1_BUTTON),
                         abs(tock - sw1),
                         xdigitalRead(SW2_BUTTON),
                         abs(tock - sw2),
                         xigitalRead(RELAY_GPIO),
                         xigitalRead(TRIAC_GPIO)
                         );
            last = millis(); sw1 = sw2 = tock = 0;
        }
    }
#endif
    
    {	static unsigned long last = 0;
        if (millis() - last > _report_period) {
            last = millis();
            JsonDocument jsonDoc;
            JsonObject out = jsonDoc.to<JsonObject>();
            report(out);
            
            String buff;
            serializeJson(jsonDoc, buff);
            //        if (buff.length() > MAX_MSG) buff = buff.substring(0, MAX_MSG);
            Log.println(buff);
        }
    }
    // XX to hook into a callback of the ethernet/wifi
    // once we figure out how we can get this from the wifi.
    //
    static bool lastconnectedstate = false;
    bool connectedstate = isConnected();
    if (lastconnectedstate != connectedstate) {
        if (connectedstate) {
            if (_connect_callback)
                _connect_callback();
        } else {
            if (_disconnect_callback)
                _disconnect_callback();
        };
        lastconnectedstate = connectedstate;
    };
    
    Log.loop();
    Debug.loop();
    
    if(isConnected())
        mqttLoop();
    
    // Note that this will also run the security and ohter handlers; see
    // addSecurityHandler().
    //
    std::list<ACBase *>::iterator it;
    
    for (it =_handlers.begin(); it!=_handlers.end(); ++it) {
        (*it)->loop();
    }
}

void ACNodeBase::delayedReboot() {
    static int warn_counter = 0;
    static unsigned long last = 0;
    
    if (millis() - last < 1000)
        return;
    
    if (warn_counter > 5) {
        Log.println("Forced reboot NOW");
        ESP.restart();
    };
    
    char buff[255];
    snprintf(buff,MAX_MSG,"Countdown to forced reboot: %d", 5 - warn_counter);
    
    Log.println(buff);
    
    last = millis();
    warn_counter ++;
}



void ACNodeBase::checkClearEEPromAndCacheButtonPressed(uint8_t button) {
    const unsigned long prevSecs = MAX_WAIT_TIME_BUTTON_PRESSED / 1000;
    
    if (button == 255)
        return;
    
    // check button pressed
    pinMode(button, button);
    
    // check if button is pressed for at least 3 s
    Log.printf("Hold button for %d seconds to clearing EEProm and cache.\n", prevSecs);
    
    if (xdigitalRead(button) != CLEAR_EEPROM_AND_CACHE_BUTTON_PRESSED)
        return;
    
    unsigned long _start = millis();
    while (xdigitalRead(button) == CLEAR_EEPROM_AND_CACHE_BUTTON_PRESSED) {
        if ((millis() - _start) > MAX_WAIT_TIME_BUTTON_PRESSED) {
            // Clear EEPROM
            EEPROM.begin(1024);
            wipe_eeprom();
            Log.println("EEProm cleared!");
            
            // Clear cache
            prepareCache(true);
            Log.println("Cache cleared!");
            
            Log.println("Node rebooting");
            ESP.restart();
        };
    }
    Log.println("Button was not (or not long enough) pressed to clear EEProm and cache\n");
    return;
}

String ACNodeBase::uptime() {
    unsigned long up = uptimeInSeconds();
    String unit = "s";
    if (!up)
        return "unknown";
    if (up > 300) { up /= 60; unit = "m";
        if (up > 180) { up /= 60; unit = "h";
            if (up > 50) { up /= 24; unit = "d";
                if (up > 1000) { up /= 30.42; unit = "m";
                }; }; }; };
    return String(up) + unit;
}
