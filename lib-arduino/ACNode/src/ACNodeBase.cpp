#include <ACNode.h>
#include "ConfigPortal.h"
#include <EEPROM.h>
#include <ArduinoJSON.h>
#include <esp_debug_helpers.h>

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
    
    const std::shared_ptr<LOGBase> & th = std::make_shared<TelnetSerialStream>(telnetSerialStream);
    const std::shared_ptr<LOGBase> & wh = std::make_shared<WebSerialStream>(webSerialStream);
    
    Log.addPrintStream(th);
    Log.addPrintStream(wh);

    Debug.addPrintStream(wh);
    Debug.addPrintStream(th);
#ifdef SYSLOG_HOST
  syslogStream.setDestination(SYSLOG_HOST);
  syslogStream.setRaw(true);
#ifdef SYSLOG_PORT
  syslogStream.setPort(SYSLOG_PORT);
#endif
    Log.addPrintStream(std::make_shared<SyslogStream>(syslogStream));
    Debug.addPrintStream(std::make_shared<SyslogStream>(syslogStream));
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

String ACNodeBase::chipId() {
#ifdef ESP32
    uint64_t chipid = ESP.getEfuseMac();
    // We can't do 64 bit straight to string.
    uint32_t low = chipid & 0xFFFFFFFF;
    uint32_t high = chipid >> 32;
    char buff[16+1];
    snprintf(buff,sizeof(buff),"%08ul%08ul", high, low);
#else
    uint32_t chipid = ESP.getChipId();
    char buff[8+1];
    snprintf(buff,sizeof(buff),"%08ul",chipid);
#endif
    return String(chipid);
};


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
#ifdef PROFILE_BASE
//    Debug.printf("addHandler(%p,%s)\n", handler, handler->name());
//    esp_backtrace_print(100);
#endif
    _handlers.insert (_handlers.end(), handler);
}

void ACNodeBase::begin(eth_board_t board /* default is BOARD_AART */, uint8_t clear_button) {
    _begin(board, clear_button);
    _complete_begin(clear_button);
}

void ACNodeBase::_complete_begin(uint8_t clear_button) {
    std::list<ACBase *>::iterator it;
    for (it =_handlers.begin(); it!=_handlers.end(); ++it) {
#ifdef PROFILE_BASE
        (*it)->micros_in_loop = 0;
#endif
        Debug.printf("%s->begin()\n", (*it)->name());
        (*it)->begin();
    }

    // We need things like OTA full set up. So we do this as
    // 'late' as we can.
    //
    Log.println("MDNS Responder started");
    MDNS.begin(moi);
    Log.printf("Host details %s (%s)\n", moi, String(localIP()).c_str());
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
    
    _espClient = WiFiClient();
    _client = PubSubClient(_espClient);
    _client.setServer(mqtt_server, mqtt_port);

    // It is safe to start logging early - as these won't emit anyting until
    // the network is known to be up.
    //
    char topic[256];
    snprintf(topic, sizeof(topic), "%s/%s/%s", mqtt_topic_prefix, logpath, moi);

    mqttlogStream = new MqttStream(&_client, topic);    
    // const std::shared_ptr<LOGBase> & mh = std::make_shared<MqttStream>(*mqttlogStream);
    // Log.addPrintStream(mh);

    if (moi == NULL || *moi == 0)
        strncpy(moi,"no-mqtt-id",sizeof(moi));
    
    if (mqtt_port ==0)
        mqtt_port = MQTT_DEFAULT_PORT;

    _client.setServer(mqtt_server, mqtt_port);
    Log.println("PubSubClient initialized");

#if 0
    reconnectMQTT();
    mqttLoop();
#endif
    

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

void ACNodeBase::checkClearEEPromAndCacheButtonPressed(unsigned char button) {};

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
    
#ifdef PROFILE_BASE
    static unsigned long lst = 0;
    bool show = (millis() - lst) > 100 *1000;
    if (show)
        Debug.println("Profile (in microSeconds):");
    for (it =_handlers.begin(); it!=_handlers.end(); ++it) {
        unsigned long s = micros();
        (*it)->loop();
        unsigned long delta = micros() - s;
        
        if ((*it)->micros_in_loop == 0)
            (*it)->micros_in_loop = delta;
        (*it)->micros_in_loop = ((*it)->micros_in_loop * 50 + delta)/51;
        
        if (show)
            Debug.printf("   %12lu %08x %s\n",(*it)->micros_in_loop,(*it), (*it)->name());
    };
    if (show) {
        lst = millis();
        Debug.println("-----");
    };
#else
    for (it =_handlers.begin(); it!=_handlers.end(); ++it) {
        (*it)->loop();
    }
#endif
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


String since(unsigned long up) {
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

String ACNodeBase::uptime() {
    unsigned long up = uptimeInSeconds();
    return since(up);
};

const char * ACNodeBase::state2str(int state) {
#if __ATMEL_8BIT
    static char buff[10]; snprintf(buff, sizeof(buff), "Error: %d", state);
    return buff;
#else
    switch (state) {
        case  /* -4 */ MQTT_CONNECTION_TIMEOUT:
            return "the server didn't respond within the keepalive time";
        case  /* -3 */ MQTT_CONNECTION_LOST :
            return "the network connection was broken";
        case  /* -2 */ MQTT_CONNECT_FAILED :
            return "the network connection failed";
        case  /* -1  */ MQTT_DISCONNECTED :
            return "the client is disconnected (clean)";
        case  /* 0  */ MQTT_CONNECTED :
            return "the client is connected";
        case  /* 1  */ MQTT_CONNECT_BAD_PROTOCOL :
            return "the server doesn't support the requested version of MQTT";
        case  /* 2  */ MQTT_CONNECT_BAD_CLIENT_ID :
            return "the server rejected the client identifier";
        case  /* 3  */ MQTT_CONNECT_UNAVAILABLE :
            return "the server was unable to accept the connection";
        case  /* 4  */ MQTT_CONNECT_BAD_CREDENTIALS :
            return "the username/password were rejected";
        case  /* 5  */ MQTT_CONNECT_UNAUTHORIZED :
            return "the client was not authorized to connect";
        default:
            break;
    }
    return "Unknown MQTT error";
#endif
}


void ACNodeBase::reconnectMQTT() {
    if (_client.getBufferSize() < MAX_MSG)
        if (!_client.setBufferSize(MAX_MSG))
            Log.println("WARNING - buffer size could not be increased to a large enough value. All things may go wrong.");
    
    Log.printf("Connecting <%s> to %s:%d (MQTT State : %s)\n",
               moi, mqtt_server, mqtt_port,
               state2str(_client.state()));
    
    if (!_client.connect(moi)) {
        Log.print("Reconnect failed : ");
        Log.println(state2str(_client.state()));
        return;
    }
    _client.loop();
    
    Debug.println("(re)connected ");
    _mqtt_reconnects ++;
}


bool ACNodeBase::isUp() {
    return _client.connected();
}

void ACNodeBase::mqttLoop() {
    static unsigned long last_mqtt_connect_try = 0;
    _client.loop();

    if (!isConnected())
        return;
    
    if (!isUp()) {
        // report transient error ? Which ? And how often ?
        if (millis() - last_mqtt_connect_try > 10000 || last_mqtt_connect_try == 0) {
            Log.printf("Reconnect as MQTT is no longer up\n");
            reconnectMQTT();
            last_mqtt_connect_try = millis();
        }
        return;
    };
}
