#ifndef _H_ACNODE_PRIVATE
#define _H_ACNODE_PRIVATE

#ifdef  ESP32
#  include <ESPmDNS.h>
#  include <WiFiUdp.h>
#  include <ESPAsyncWebServer.h>
#  include "WiredEthernet.h"
#  include <esp32-hal-gpio.h> // digitalWrite and friends.
#else
#  include <ESP8266WiFi.h>
#  include <WiFiClient.h>
#  include <ESPAsyncWebServer.h>
#endif

#include <SPI.h>
#include <PubSubClient.h>        // https://github.com/knolleary/
#include <base64.h>
#include <Crypto.h>

#include <TLog.h>
#include <MqttlogStream.h>
#include <TelnetSerialStream.h>
#include <WebSerialStream.h>

#include <ArduinoJson.h>

#include "mbedtls/sha256.h" /* SHA-256 only */
#include "mbedtls/md.h"     /* generic interface */

#include <list>
#include <vector>
#include <algorithm>    // std::find

#include "util/common-utils.h"
#include "ACBase.h"
#include "LED.h"
#include "RFID.h" // for the max tag size


#ifndef MQTT_SERVER
#define MQTT_SERVER      "spacebus.makerspaceleiden.nl"
#endif

#ifndef MQTT_TOPIC_PREFIX
#define MQTT_TOPIC_PREFIX "test"
#endif

#ifndef MQTT_TOPIC_LOG
#define MQTT_TOPIC_LOG        "log"
#endif

#ifndef MQTT_TOPIC_MASTER
#define MQTT_TOPIC_MASTER "master"
#endif

#ifndef MQTT_PREXIX
#define MQTT_PREFIX "ac"
#endif

#ifndef MQTT_DEFAULT_PORT
#define MQTT_DEFAULT_PORT (1883)
#endif


#define Trace if (0) Debug

#define REPORT_PERIOD (5*60*1000) 	// Every 5 minutes - also triggers alarm in monitoring when awol

// typedef unsigned long beat_t;
// extern beat_t beatCounter;      // My own timestamp - manually kept due to SPI timing issues.

#define LOG_SERIAL	(1<<0)
#define LOG_TELNET	(1<<1)
#define LOG_SYSLOG	(1<<2)
#define LOG_WEBBROWSER	(1<<3)
#define LOG_MQTT	(1<<4)

#ifndef LOG_DEST_DEFAULT
#ifdef ESP32
#define LOG_DEST_DEFAULT (LOG_SERIAL | LOG_TELNET | LOG_MQTT | LOG_SYSLOG | LOG_WEBBROWSER)
#else
#define LOG_DEST_DEFAULT (LOG_SERIAL | LOG_TELNET | LOG_MQTT)
#endif
#endif

typedef enum {
    ACNODE_ERROR_FATAL,
} acnode_error_t;

typedef enum {
    ACNODE_FATAL,
    ACNODE_ERROR,
    ACNODE_WARN,
    ACNODE_INFO,
    ACNODE_VERBOSE,
    ACNODE_DEBUG
} acnode_loglevel_t;

typedef enum {
    BOARD_AART,  	// https://wiki.makerspaceleiden.nl/mediawiki/index.php/POESP-board_1.0
    BOARD_OLIMEX, 	//  https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1
    BOARD_NG	    // https://github.com/dirkx/rfid-oled-esp32 (black, white, green, red and purple)
} eth_board_t;

typedef enum {
    PROTO_REST,     // experimental - variation of https://wiki.makerspaceleiden.nl/mediawiki/index.php/Payment_and_Paring_REST_protocol
    PROTO_SIG2,     // Mqtt used; in use
    PROTO_SIG1,     // Used by second generator Aart nodes, no longer in use (2015?)
    PROTO_MSL,      // Used by first to generation raspPi nodes, no longer in use
    PROTO_NONE
} acnode_proto_t;

// Clear EEProm + Cache button
// Press BUT1 on Olimex ESP32 PoE module before (re)boot of node
// keep BUT1 pressed for at least 5 s
// After the release of BUT1 node will restart with empty EEProm and empty cache

#define CLEAR_EEPROM_AND_CACHE_BUTTON_PRESSED   (LOW)
#define MAX_WAIT_TIME_BUTTON_PRESSED            (4000)  // in ms

class ACNodeBase : public ACBase {
public:
    ACNodeBase(const char * machine, const char * ssid, const char * ssid_passwd);
    ACNodeBase(const char * machine = NULL, bool wired = true);
    ~ACNodeBase() { Serial.println("DESTROY ACNodeBase should enver happen"); };

    virtual const char * name() { return "ACNodeBase"; }
    
    void set_report_period(const unsigned long period) { _report_period = period; };
    void set_mqtt_host(const char *p);
    void set_mqtt_port(uint16_t p);
    void set_mqtt_prefix(const char *p);
    void set_mqtt_log(const char *p);
    
    void set_moi(const char *p);
    void set_machine(const char *p);
    void set_master(const char *p);
    
    uint16_t mqtt_port;
    char moi[MAX_NAME];
    char mqtt_server[MAX_HOST];
    char machine[MAX_NAME];
    char logpath[MAX_NAME];
    IPAddress localIP();
    String getHostname();
    String macAddressString();
    String chipId();
    
    void delayedReboot();
   
    AsyncWebServer * webServer() { return _webServer; };
    String urlLogPrefix() { return "/"; };
 
    // Callbacks.
    typedef std::function<void(acnode_error_t)> THandlerFunction_Error;
    ACNodeBase& onError(THandlerFunction_Error fn)
    { _error_callback = fn; return *this; };
    
    typedef std::function<void(void)> THandlerFunction_Connect;
    ACNodeBase& onConnect(THandlerFunction_Connect fn)
    { _connect_callback = fn; return *this; };
    
    typedef std::function<void(void)> THandlerFunction_Disconnect;
    ACNodeBase& onDisconnect(THandlerFunction_Disconnect fn)
    { _disconnect_callback = fn; return *this; };
    
    typedef std::function<cmd_result_t(const char *cmd, const char * rest)> THandlerFunction_Command;
    ACNodeBase& onValidatedCmd(THandlerFunction_Command fn)
    { _command_callback = fn; return *this; };
    
    typedef std::function<void(const char *msg)> THandlerFunction_SimpleCallback;
    ACNodeBase& onApproval(THandlerFunction_SimpleCallback fn)
    { _approved_callback = fn; return *this; };
    
    ACNodeBase& onDenied(THandlerFunction_SimpleCallback fn)
    { _denied_callback = fn; return *this; };
    
    typedef std::function<void(JsonObject &report)> THandlerFunction_Report;
    void onReport(THandlerFunction_Report fn)
    { _report_callback = fn; return; };
    
    void loop();
    void begin(eth_board_t board = BOARD_AART, uint8_t clear_button = -1);
    void addHandler(ACBase *handler);
    
    String uptime();
    
    unsigned long uptimeInSeconds() { return _start_beat ? (time(NULL) - _start_beat) : 0; };
    void set_debugAlive(bool debug);
    void set_log_destinations(unsigned int destinations);
    void set_debug_destinations(unsigned int destinations);
    bool isConnected(); // ethernet/wifi is up with valid IP.
    bool isUp(); // MQTT et.al also running.
    
    // This function should be private - but we're calling
    // it from a C callback in the mqtt subsystem. And only
    // when we listen.
    virtual void process(const char * topic, const char * payload) {
        Log.printf("%s: Not IMPLEMENTED\n", __PRETTY_FUNCTION__);
    }

    void report(JsonObject & report);
   
    PubSubClient _client = PubSubClient(_espClient);
    char mqtt_topic_prefix[MAX_NAME];
    char master[MAX_NAME];
    void configureMQTT();
    void reconnectMQTT();
    void mqttLoop();
    
    virtual void request_approval(const char * tag, const char * operation = NULL, const char * target = NULL, bool useCacheOk= true) { 
        Log.printf("%s: Not IMPLEMENTED\n", __PRETTY_FUNCTION__);
    };
    beat_t _lastSwipe;

    bool wired() { return _wired; }

    void pop();
    void CONSTS();

    THandlerFunction_Error _error_callback;
    THandlerFunction_Connect _connect_callback;
    THandlerFunction_Command _command_callback;
    THandlerFunction_Disconnect _disconnect_callback;
    THandlerFunction_Report _report_callback;

    // We register a bunch of handlers - rather than calling them
    // directly with a flag trigger -- as this allows the linker
    // to not link in unused functionality. Thus making the firmware
    // small enough for the ESP and ENC+Arduino versions.
    //
    std::list<ACBase *> _handlers;

private:
    unsigned int log_destinations = LOG_DEST_DEFAULT;
    bool _debug_alive, _debug;
    
    WiFiClient _espClient;
    
    void checkClearEEPromAndCacheButtonPressed(uint8_t button);
    
    const char * state2str(int state);
    AsyncWebServer * _webServer;
    
protected:
    THandlerFunction_SimpleCallback _approved_callback, _denied_callback;

    const char * _ssid;
    const char * _ssid_passwd;
    unsigned long _report_period;
    bool _wired;
    acnode_proto_t _proto;
    char _lasttag[RFID_MAX_TAG_LEN * 4];      // Up to a 3 digit byte and a dash or terminating \0. */
    // stat counters
    unsigned long _approve, _deny, _reqs, _mqtt_reconnects, _start_beat;
    
    void _complete_begin(uint8_t clear_button = -1);
    void _begin(eth_board_t board = BOARD_AART, uint8_t clear_button = -1);
};

class ACNode : public ACNodeBase {
public:
    ACNode(const char * machine, const char * ssid, const char * ssid_passwd, acnode_proto_t proto = PROTO_SIG2);
    ACNode(const char * machine = NULL, bool wired = true, acnode_proto_t proto = PROTO_SIG2);

#ifdef HAS_SIG2
    void add_trusted_node(const char *node);
#endif

    void addSecurityHandler(ACSecurityHandler *handler);
   
    char * cloak(char *tag);
    void send_helo(char * tokenOrNull = NULL);

    unsigned long uptimeInSeconds() { return _start_beat ?  beatCounter - _start_beat : 0; };

    // Public - so it can be called from our fake
    // singleton. Once that it solved it should really
    // become private again.
    //
    void send(const char * payload) { send(NULL, payload, false); };
    void send(const char * topic, const char * payload, bool raw = false);


    // This function should be private - but we're calling
    // it from a C callback in the mqtt subsystem.
    //
    void process(const char * topic, const char * payload);


private:
    unsigned int log_destinations = LOG_DEST_DEFAULT;
    bool _debug_alive, _debug;
    std::shared_ptr<LOGBase> wh, th;
//    MqttStream * mqttlogStream;

protected:
    acnode_proto_t _proto;
};

String since(unsigned long up);

// Unfortunately - MQTT callbacks cannot yet pass
// a pointer. So we need a 'global' variable; and
// sort of treat this class as a singleton. And
// contain this leakage to just a few functions.
//
extern ACNodeBase *_acnodebase;
#endif
