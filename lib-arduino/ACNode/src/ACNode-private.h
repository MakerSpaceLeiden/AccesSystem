#ifndef _H_ACNODE_PRIVATE
#define _H_ACNODE_PRIVATE

#include <OlimexBoard.h>         

#ifdef  ESP32
//#  include <WiFi.h>
#  include <ESPmDNS.h>
#  include <WiFiUdp.h>
#  include "WiredEthernet.h"
#  include <esp32-hal-gpio.h> // digitalWrite and friends L	.
#else
#  include <ESP8266WiFi.h>
#endif

#include <PubSubClient.h>        // https://github.com/knolleary/
#include <TLog.h>
#include <MqttlogStream.h>
#include <TelnetSerialStream.h>
#include <WebSerialStream.h>
#include <SPI.h>

#include <base64.h>
#include <Crypto.h>
#include "mbedtls/sha256.h" /* SHA-256 only */
#include "mbedtls/md.h"     /* generic interface */

#include <list>
#include <vector>
#include <algorithm>    // std::find

#include <common-utils.h>
#include <ACBase.h>
#include <LED.h>

#include <ArduinoJson.h>
#include "RFID.h" // for the max tag size

extern char * strsepspace(char **p);

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
	BOARD_OLIMEX 	// https://www.olimex.com/Products/IoT/ESP32/ESP32-POE/open-source-hardware
} eth_board_t;

// #define HAS_MSL
// #define HAS_SIG1
#define HAS_SIG2

typedef enum { PROTO_SIG2, PROTO_SIG1, PROTO_MSL, PROTO_NONE } acnode_proto_t;

class ACNode : public ACBase {
public:
    ACNode(const char * machine, const char * ssid, const char * ssid_passwd, acnode_proto_t proto = PROTO_SIG2);
    ACNode(const char * machine = NULL, bool wired = true, acnode_proto_t proto = PROTO_SIG2);

    const char * name() { return "ACNode"; }

    void set_report_period(const unsigned long period) { _report_period = period; };
    void set_mqtt_host(const char *p);
    void set_mqtt_port(uint16_t p);
    void set_mqtt_prefix(const char *p);
    void set_mqtt_log(const char *p);

#ifdef HAS_SIG2
    void add_trusted_node(const char *node);
#endif

    void set_moi(const char *p);
    void set_machine(const char *p);
    void set_master(const char *p);

    uint16_t mqtt_port;
    char moi[MAX_NAME];
    char mqtt_server[MAX_HOST];
    char machine[MAX_NAME];
    char master[MAX_NAME];
    char logpath[MAX_NAME];
    char mqtt_topic_prefix[MAX_NAME];
    
    IPAddress localIP() { 
#ifdef ESP32
	if (_wired) 
	    return ETH.localIP(); 
	else 
#endif 
	    return WiFi.localIP(); 
    };
    String macAddressString() { 
#ifdef ESP32
         if (_wired) 
            return ETH.macAddress(); 
         else 
#endif
	    return WiFi.macAddress(); 
    };
    String chipId() {
#ifdef ESP32
                uint64_t chipid = ESP.getEfuseMac();
                // We can't do 64 bit straight to string.
                uint32_t low = chipid & 0xFFFFFFFF;
                uint32_t high = chipid >> 32;
                return String(high, HEX) + String(low, HEX);
#else
                uint32_t chipid = ESP.getChipId();
                return String(chipid);
#endif
    };

    void delayedReboot();
 
    // Callbacks.
    typedef std::function<void(acnode_error_t)> THandlerFunction_Error;
    ACNode& onError(THandlerFunction_Error fn)
    	    { _error_callback = fn; return *this; };
    
    typedef std::function<void(void)> THandlerFunction_Connect;
    ACNode& onConnect(THandlerFunction_Connect fn)
	    { _connect_callback = fn; return *this; };
    
    typedef std::function<void(void)> THandlerFunction_Disconnect;
    ACNode& onDisconnect(THandlerFunction_Disconnect fn)
	    { _disconnect_callback = fn; return *this; };
    
    typedef std::function<cmd_result_t(const char *cmd, const char * rest)> THandlerFunction_Command;
    ACNode& onValidatedCmd(THandlerFunction_Command fn)
	    { _command_callback = fn; return *this; };

    typedef std::function<void(const char *msg)> THandlerFunction_SimpleCallback;
    ACNode& onApproval(THandlerFunction_SimpleCallback fn)
	    { _approved_callback = fn; return *this; };
    ACNode& onDenied(THandlerFunction_SimpleCallback fn)
	    { _denied_callback = fn; return *this; };
    
    typedef std::function<void(JsonObject &report)> THandlerFunction_Report;
    void onReport(THandlerFunction_Report fn)
            { _report_callback = fn; return; };

    void loop();
    void begin(eth_board_t board = BOARD_AART);
    cmd_result_t handle_cmd(ACRequest * req);
   
    void addHandler(ACBase *handler);
    void addSecurityHandler(ACSecurityHandler *handler);
   
    void request_approval(const char * tag, const char * operation = NULL, const char * target = NULL, bool useCacheOk= true);

    char * cloak(char *tag);
    
    void set_debugAlive(bool debug);
    void set_log_destinations(unsigned int destinations);
    void set_debug_destinations(unsigned int destinations);
    bool isConnected(); // ethernet/wifi is up with valid IP.
    bool isUp(); // MQTT et.al also running.
    
    void send_helo(char * tokenOrNull = NULL);

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
   
    
    PubSubClient _client;
private:
    unsigned int log_destinations = LOG_DEST_DEFAULT;
    bool _debug_alive, _debug;
    THandlerFunction_Error _error_callback;
    THandlerFunction_Connect _connect_callback;
    THandlerFunction_Disconnect _disconnect_callback;
    THandlerFunction_SimpleCallback _approved_callback, _denied_callback;
    THandlerFunction_Command _command_callback;
    THandlerFunction_Report _report_callback;

    beat_t _lastSwipe;    
    WiFiClient _espClient;
    
    void configureMQTT();
    void reconnectMQTT();
    void mqttLoop();
    void pop();

    const char * state2str(int state);
    
    // We register a bunch of handlers - rather than calling them
    // directly with a flag trigger -- as this allows the linker
    // to not link in unused functionality. Thus making the firmware
    // small enough for the ESP and ENC+Arduino versions.
    //
    std::list<ACBase *> _handlers;
    std::list<ACSecurityHandler*> _security_handlers;
protected:
    const char * _ssid;
    const char * _ssid_passwd;
    unsigned long _report_period;
    bool _wired;
    acnode_proto_t _proto;
    char _lasttag[RFID_MAX_TAG_LEN * 4];      // Up to a 3 digit byte and a dash or terminating \0. */
// stat counters
   unsigned long _approve, _deny, _reqs, _mqtt_reconnects, _start_beat;
};

// Unfortunately - MQTT callbacks cannot yet pass
// a pointer. So we need a 'global' variable; and
// sort of treat this class as a singleton. And
// contain this leakage to just a few functions.
//
extern ACNode *_acnode;

extern void send(const char * topic, const char * payload);

extern const char ACNODE_CAPS[];

#include <MSL.h>
#include <SIG1.h>
#include <SIG2.h>

#include <Beat.h>
#include <OTA.h>

#endif
