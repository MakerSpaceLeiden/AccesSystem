#ifndef _H_ACNODE
#define _H_ACNODE

// #include <Ethernet.h>
#ifdef  ESP32
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ETH.h>
#include "WiredEthernet.h"

#include <esp32-hal-gpio.h> // digitalWrite and friends L	.
#else
#include <ESP8266WiFi.h>
#endif
#include <PubSubClient.h>        // https://github.com/knolleary/

#include <SPI.h>

#include <base64.hpp>
#include <Crypto.h>
#include <SHA256.h>

#include <list>
#include <vector>

#include <ACBase.h>

#define B64D(base64str, bin, what) { \
if (decode_base64_length((unsigned char *)base64str) != sizeof(bin)) { \
Debug.printf("Wrong length " what " (expected %d, got %d/%s) - ignoring\n", \
	sizeof(bin), decode_base64_length((unsigned char *)base64str), base64str); \
return false; \
}; \
decode_base64((unsigned char *)base64str, bin); \
}

#define SEP(tok, err, errorOnReturn) \
char *  tok = strsepspace(&p); \
if (!tok) { \
Debug.printf("Malformed/missing " err ": %s]n", p ); \
return errorOnReturn; \
}
extern char * strsepspace(char **p);
extern const char *machinestateName[];

// typedef unsigned long beat_t;
// extern beat_t beatCounter;      // My own timestamp - manually kept due to SPI timing issues.

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

class ACLog : public Print {
public:
    void addPrintStream(const std::shared_ptr<Print> &_handler) {
        handlers.push_back(_handler);
    };
    size_t write(byte a) {
        for (auto it = begin (handlers); it != end (handlers); ++it) {
            (*it)->write(a);
        }
        return Serial.write(a);
    }
private:
    std::vector<std::shared_ptr<Print> > handlers;
};

class ACNode : public ACBase {
public:
    const char * name() { return "ACNode"; }

    uint16_t mqtt_port;
    char moi[MAX_NAME];
    char mqtt_server[MAX_HOST];
    char machine[MAX_NAME];
    char master[MAX_NAME];
    char logpath[MAX_NAME];
    char mqtt_topic_prefix[MAX_NAME];
    
    ACNode(const char * ssid, const char * ssid_passwd);
    ACNode(bool wired);
    
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
    
    typedef std::function<void(const char *cmd, const char * rest)> THandlerFunction_Command;
    ACNode& onValidatedCmd(THandlerFunction_Command fn)
    { _disconnect_command = fn; return *this; };
    
    void loop();
    void begin();
    cmd_result_t handle_cmd(ACRequest * req);
    
    void addHandler(ACBase *handler);
    void addSecurityHandler(ACSecurityHandler *handler);
    
    char * cloak(char tag[MAX_MSG]);
    
    
    void set_debugAlive(bool debug);
    bool isConnected(); // ethernet/wifi is up with valid IP.
    bool isUp(); // MQTT et.al also running.
    
    // Public - so it can be called from our fake
    // singleton. Once that it solved it should really
    // become private again.
    //
    void send(const char * topic, const char * payload, bool raw = false);
    
    // This function should be private - but we're calling
    // it from a C callback in the mqtt subsystem.
    //
    void process(const char * topic, const char * payload);
    
private:
    
    bool _debug_alive;
    THandlerFunction_Error _error_callback;
    THandlerFunction_Connect _connect_callback;
    THandlerFunction_Disconnect _disconnect_callback;
    THandlerFunction_Command _disconnect_command;
    
    WiFiClient _espClient;
    // EthernetClient _ethClient;
    PubSubClient _client;
    
    void configureMQTT();
    void reconnectMQTT();
    void mqttLoop();
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
    bool _wired;
};

// Unfortunately - MQTT callbacks cannot yet pass
// a pointer. So we need a 'global' variable; and
// sort of treat this class as a singleton. And
// contain this leakage to just a few functions.
//
extern ACNode *_acnode;
extern ACLog Log;
extern ACLog Debug;

extern void send(const char * topic, const char * payload);

extern const char ACNODE_CAPS[];

#endif
