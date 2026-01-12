#include <ACNode.h>
#include "ConfigPortal.h"
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <esp_debug_helpers.h>
#include "util/part.h"
#include "util/reset.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "esp_sntp.h"

SET_LOOP_TASK_STACK_SIZE(16*1024);

#ifdef ESP32
#include <WiFi.h>
#include <ETH.h>
#endif

#include <WebSerialStream.h>
#include <TelnetSerialStream.h>
#include <MqttlogStream.h>

#ifdef SYSLOG_HOST
#include <SyslogStream.h>
// SyslogStream syslogStream = SyslogStream();
#endif

#include "WhiteNodeIndexPage.h"

beat_t beatCounter = 0;      // My own timestamp - manually kept due to SPI timing issues.

float loopRate = 0;

const char * reset_core0, *reset_core1;
// Unfortunately - MQTT callbacks cannot yet pass
// a pointer. So we need a 'global' variable; and
// sort of treat this class as a singleton. And
// contain this leakage to just a few functions.
//
ACNodeBase *_acnodebase = NULL;

void ACNodeBase::set_mqtt_host(const char *p) {
    safestrncpy(mqtt_server,p, sizeof(mqtt_server));
};
void ACNodeBase::set_mqtt_port(uint16_t p)  { mqtt_port = p; };

void ACNodeBase::set_mqtt_prefix(const char *p)  {
    safestrncpy(mqtt_topic_prefix,p, sizeof(mqtt_topic_prefix));
}
void ACNodeBase::set_mqtt_log(const char *p)  { safestrncpy(logpath,p, sizeof(logpath)); };
void ACNodeBase::set_moi(const char *p)  { safestrncpy(moi,p, sizeof(moi)); };
void ACNodeBase::set_machine(const char *p)  { safestrncpy(machine,p, sizeof(machine)); };
void ACNodeBase::set_master(const char *p)  { safestrncpy(master,p, sizeof(master)); };

static char mqtt_moi[24];

#include "ACNodeBaseStatusPage.h"

void ACNodeBase::CONSTS() {
    if (_acnodebase) {
	Serial.printf("Unexpected rentry %p == %p\n",_acnodebase,this);
	return;
    };
	
    _acnodebase = this;

    Serial.begin(115200);
    while(!Serial) { delay(10); };

    reset_core0 = reset_reason(0);
    reset_core1 = reset_reason(1);

    Serial.println("\n\nBoot started -- " __DATE__ " - " __TIME__);
};

void ACNodeBase::pop() {
    safestrncpy(mqtt_server, MQTT_SERVER, sizeof(mqtt_server));
    mqtt_port = MQTT_DEFAULT_PORT;
    _report_period = REPORT_PERIOD;
    
    moi[0] = 0;
    if (machine[0] == 0)
        safestrncpy(machine, String("test-" + chipId() ).c_str(), sizeof(machine));
    if (moi[0] == 0)
        safestrncpy(moi,machine,sizeof(moi));
   
    safestrncpy(mqtt_topic_prefix, MQTT_TOPIC_PREFIX, sizeof(mqtt_topic_prefix));
    safestrncpy(master, MQTT_TOPIC_MASTER, sizeof(master));
    safestrncpy(mqtt_topic_prefix, MQTT_PREFIX, sizeof(mqtt_topic_prefix));
    safestrncpy(logpath, MQTT_TOPIC_LOG, sizeof(logpath));

    // Set a sensible default - so we have something until
    // DHCP/ntp kick in.
    setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1);
    tzset();
   
    _webServer = new AsyncWebServer(80);
    _webServer->on("/",  HTTP_GET, [this](AsyncWebServerRequest *request) {
         request->send(200, "text/html", 
		(uint8_t *)htmlIndexPageContent, htmlIndexPageContentLength,
		[this](const String &var) -> String {
                    time_t now = time(NULL);
		    if (var == "NODE") 
		      return moi;
		    if (var == "TIME") 
		      return ctime(&now);
    		    return emptyString;
		});
    });
    _webServer->on("/state.json", HTTP_GET, [this](AsyncWebServerRequest *request) {
         AsyncResponseStream *response = request->beginResponseStream("application/json");

         JsonDocument doc;
         JsonObject out = doc.to<JsonObject>();
         report(out);

         serializeJson(out, *response);
         request->send(response);
    });
    _webServer->on("/state",  HTTP_GET, [this](AsyncWebServerRequest *request) {
         request->send(200, "text/html", (uint8_t *)htmlStatusPageContent, htmlStatusPageContentLength);
    });

    Log.setTimestamp(true); 
    Log.setIdentifier("LOG");

    Debug.setTimestamp(true); 
    Debug.setIdentifier("DBG");

    const std::shared_ptr<LOGBase> & wh = std::make_shared<TelnetSerialStream>();
    Log.addPrintStream(wh);
    Debug.addPrintStream(wh);

    const std::shared_ptr<LOGBase> & th = std::make_shared<WebSerialStream>(webServer(),"/log");
    Debug.addPrintStream(th);
    Log.addPrintStream(th);

#ifdef SYSLOG_HOST
    const std::shared_ptr<SyslogStream> & syslogStream = std::make_shared<SyslogStream>();
    syslogStream->setDestination(SYSLOG_HOST);
    syslogStream->setRaw(true);
#ifdef SYSLOG_PORT
    syslogStream->setPort(SYSLOG_PORT);
#endif
    Log.addPrintStream(syslogStream);
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
        safestrncpy(machine,m, sizeof(machine));
    CONSTS();
    pop();
}

ACNodeBase::ACNodeBase(const char *m, const char * ssid , const char * ssid_passwd ) :
_ssid(ssid), _ssid_passwd(ssid_passwd), _wired(false)
{
    if (m && *m)
        safestrncpy(machine,m, sizeof(machine));
    CONSTS();
    pop();
}

String ACNodeBase::chipId() {
    char buff[48];
#ifdef ESP32
    uint64_t chipid = ESP.getEfuseMac();
    // We can't do 64 bit straight to string.
    uint32_t low = chipid & 0xFFFFFFFF;
    uint32_t high = chipid >> 32;
    safesnprintf(buff,sizeof(buff),"%08lx%08lx", high, low);
    return String(buff+4);
#else
    uint32_t chipid = ESP.getChipId();
    safesnprintf(buff,sizeof(buff),"%08x",chipid);
    return String(buff);
#endif
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
        // Debug.printf("%s->begin()\n", (*it)->name());
        (*it)->begin();
    }

    partition_info(Log); 
    _webServer->begin();
}

void ACNodeBase::_begin(eth_board_t board /* default is BOARD_AART */, uint8_t clear_button)
{
    if (!*machine)
        safestrncpy(machine, "unset-machine-name", sizeof(machine));
    
    if (!*moi)
        safestrncpy(moi, machine, sizeof(moi));
    
    if (strncmp(moi,"test-",5) == 0) {
        char * p = moi + strlen(moi);
        safesnprintf(p,sizeof(moi)-strlen(moi),"-%s",chipId().c_str());
    };
    
#if 0
    if (_debug)
        debugFlash();
#endif
    checkClearEEPromAndCacheButtonPressed(clear_button);
  
#ifdef ESP32
    // if (_wired)
    if (true)
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
#ifdef CONFIGAP
            Log.println("Staring wifi auto connect.");
            WiFiManager wifiManager;
            wifiManager.autoConnect();
#else
            Log.println("**** WARNING - No Wifi Details/no network");
#endif
        };
    esp_sntp_servermode_dhcp(1);  
    
    const int del = 3; // seconds.
    unsigned long start = millis();
    while (!isConnected() && (millis() - start < del * 1000)) {
        delay(100);
    };
    
    if (!_wired && !isConnected()) {
        // Log.printf("No connection after %d seconds (ssid=%s). Going into config portal (debug mode);.\n", del, WiFi.SSID().c_str());
        // configPortal();
        Log.printf("No connection after %d seconds (ssid=%s)\n", del, WiFi.SSID().c_str());
    }
    else
        if(_ssid)
            Log.printf("Wifi connected to <%s>\n", WiFi.SSID().c_str());

    Log.println("MDNS Responder started");
    MDNS.begin(moi);

    if (mqtt_port ==0)
        mqtt_port = MQTT_DEFAULT_PORT;

    _client.setServer(mqtt_server, mqtt_port);

    char topic[256];
    safesnprintf(topic, sizeof(topic), "%s/%s/%s", mqtt_topic_prefix, logpath, moi);

    size_t max = MAX_MSG;

#if 0
    if (Log.maxLine() < max) {
	Log.setMaxLine(max);
	Debug.setMaxLine(max);
    };
#endif

#ifdef HAS_SIG2
    // Extra space needed for signature, beat, etc.
    max += 5 + strlen(topic) + 10;
#endif

    if (_client.getBufferSize() < max) {
	Debug.printf("MQTT: Need to increase MQTT buffer form %u to %u\n", _client.getBufferSize(), max);
        if (!_client.setBufferSize(max)) {
            Log.println("WARNING - buffer size could not be increased to a large enough value. All things may go wrong.");
	};
    };

    // It is safe to start logging early - as these won't emit anyting until
    // the network is known to be up.
    //
    const std::shared_ptr<LOGBase> & mh = std::make_shared<MqttStream>(_client, topic);
    Log.addPrintStream(mh);

    if (*moi == 0)
        safestrncpy(moi,"no-mqtt-id",sizeof(moi));
    safesnprintf(mqtt_moi,sizeof(mqtt_moi), "%06lx-%s", esp_random(),moi);

    Log.printf("MQTT: initialized mqtt://%s@%s:%d/%s\n", mqtt_moi, mqtt_server, mqtt_port, mqtt_topic_prefix);

#ifdef CONFIGAP
    configBegin();
#endif
    
    Log.begin();
    Debug.begin(); 

}

#define __(x) #x
#define _(x) __(x) 
const char _sdk[] = \
       "Arduino/"  _(ESP_ARDUINO_VERSION_MAJOR) "." _(ESP_ARDUINO_VERSION_MINOR) "." _(ESP_ARDUINO_VERSION_PATCH) \
       ", "
       "IDF/" _(ESP_IDF_VERSION_MAJOR) "." _(ESP_IDF_VERSION_MINOR) "." _(ESP_IDF_VERSION_PATCH);

const char * getHW(void) {
    static char res[48];
    if (!*res) {
	safesnprintf(res, sizeof(res),  "Arduino-" ARDUINO_BOARD "/%s.%u",
    		ESP.getChipModel(), ESP.getChipRevision());
    };
    return res;
}

void ACNodeBase::report(JsonObject out) {
    out[ "node" ] = moi;
    out[ "machine" ] = machine;
    
    JsonObject hw = out["hardware"].add<JsonObject>();
    char chipstr[30]; safestrncpy(chipstr,chipId().c_str(),sizeof(chipstr));
    hw[ "id" ] = chipstr;
    hw["loop_rate"] = loopRate;
#ifdef ESP32
    hw["coreTemp"]  = coreTemp();
#endif
    hw[ "uptime" ] = uptimeInSeconds();

    char ipstr[30]; safestrncpy(ipstr, String(localIP().toString()).c_str(),sizeof(ipstr));
    JsonObject n= out["net"].add<JsonObject>();

    n[ "ip" ] = ipstr;
    n[ "type" ] = _wired ? "UTP" : "WiFi";
    char macstr[30]; safestrncpy(macstr, macAddressString().c_str(),sizeof(macstr));
    n[ "mac" ] = macstr;

    JsonObject fw = out["build"].add<JsonObject>();
    fw[ "class" ] = name();
    fw[ "board" ] = getHW();
    fw[ "sdk" ] = _sdk;
    fw[ "rom_size_bits" ] = get_rom_size() * 8;
    fw[ "compiled" ] = __DATE__ " " __TIME__;
    
    if (_start_beat == 0)
        if (time(NULL) > 1542275849)
            _start_beat = time(NULL) + millis()/1000;

    JsonObject c = out["tags"].add<JsonObject>();
    c[ "approve" ] = _approve;
    c[ "deny" ] = _deny;
    c[ "requests" ] = _reqs;    
 
    JsonArray lr = out["lastResetReason"].add<JsonArray>();
    lr.add(reset_core0);
    lr.add(reset_core1);

    JsonObject heap = out["memory"].add<JsonObject>();
    heap["heap_free"] = ESP.getFreeHeap();
    heap["heap_free8"] = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    heap["heap_free8_min"] = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    heap["heap_free8_largest"] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    heap["stack_size"] = getArduinoLoopTaskStackSize();
   
    // attempt to track down MQTT issue.
    //
    JsonObject mq = out["mqtt"].add<JsonObject>();
    mq["mqtt_host"] = mqtt_server;
    mq["mqtt_port"] = mqtt_port;
    mq["mqtt_isUp"] = isUp();
    mq["mqtt_isConnected"] = isConnected();
    mq[ "maxMqtt" ] = MAX_MSG;
    mq[ "mqtt_reconnects" ] = _mqtt_reconnects;

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

            char topic[128];
	    safesnprintf(topic, sizeof(topic), "%s/report/%s", mqtt_topic_prefix, moi);

            JsonDocument jsonDoc;
            JsonObject out = jsonDoc.to<JsonObject>();
            report(out);

	    struct NullWriter {
                size_t write(uint8_t c) { return 1; };
                size_t write(const uint8_t *buffer, size_t length) { return length; };
            } _nullwriter;
            size_t len = serializeJson(jsonDoc, _nullwriter);

	    // We really want to avoid creating another copy of this 2k payload; as
	    // it fragments the stack. So we use a custom writer and a leaner interface
            // that does not make its own copy.
            //
            if (_client.beginPublish(topic, len, false)) {
	        struct PubSubWriter {
		    PubSubClient * _ptr;
                    size_t write(uint8_t c) { return _ptr->write(c); };
                    size_t write(const uint8_t *buffer, size_t length) { return _ptr->write(buffer,length); };
                } _pswriter = { ._ptr = &_client };
                size_t actual = serializeJson(jsonDoc, _pswriter); 
		Debug.println();
                _client.endPublish();
		if (actual != len)
			Log.printf("Only wrote %d bytes of a %d report to mqtt#%s", actual, len, topic);
            } else {
		Log.printf("Could not write report of %d bytres to mqtt#%s", len, topic);
	    };
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
        
        (*it)->micros_in_loop = ((*it)->micros_in_loop * 500 + delta)/501;
        
        if (show)
            Debug.printf("   %12lu %s\n",(*it)->micros_in_loop, (*it)->name());
    };
    if (show) {
        lst = millis();
        Debug.println("-----");
    };
#else
    for (it =_handlers.begin(); it!=_handlers.end(); ++it) 
        (*it)->loop();
#endif
    WiFiEventLoop();

    Log.loop();
    Debug.loop();

    if(isConnected()) 
        mqttLoop();
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
    
    char buff[48];
    safesnprintf(buff,sizeof(buff),"Countdown to forced reboot: %d", 5 - warn_counter);
    
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
#ifdef __ATMEL_8BIT
    static char buff[10]; safesnprintf(buff, sizeof(buff), "Error: %d", state);
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

bool ACNodeBase::isUp() {
    return _client.connected();
}

void ACNodeBase::mqttLoop() {

    _client.loop();

    if (!isConnected())
        return;

    static bool l = _client.connected();
    bool n = _client.connected();

    if (l != n) 
	Debug.printf("MQTT connection change; now %s\n", n ? "up" : "DOWN");
    l = n;
    
    if (n)
	return;

    reconnectMQTT();
}

void ACNodeBase::reconnectMQTT() {
    static unsigned long last_mqtt_connect_try = millis();
     if (millis() - last_mqtt_connect_try < 10*1000)
	return;

    last_mqtt_connect_try = millis();
    _mqtt_reconnects ++;

    Debug.printf("MQTT Connecting <%s> to %s:%d (%s)\n",
               moi, mqtt_server, mqtt_port,
               state2str(_client.state()));
    
    if (!_client.connect(mqtt_moi)) {
        Log.print("MQTT Reconnect failed : ");
        Log.println(state2str(_client.state()));
        return;
    }

    Log.printf("MQTT (re)connected to mqtt://%s@%s:%d : %s, %s\n", mqtt_moi, mqtt_server, mqtt_port, 
	_client.connected() ? "ok" : "FAIL", state2str(_client.state()));
};
