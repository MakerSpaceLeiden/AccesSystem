#ifndef _H_ACNODE_SIG
#define _H_ACNODE_SIG

#include "ACBaseNode.h"
#include "ACNode-private.h"
#include "SIG/ACNode.h"

#include <WiFiUdp.h>
#include <PubSubClient.h>        // https://github.com/knolleary/

// ArduinoJSON library -- from https://github.com/bblanchon/ArduinoJson - installed th
//
// Depending on your version - if you get an osbcure error in
// .../ArduinoJson/Polyfills/isNaN.hpp and isInfinity.hpp - then
// isnan()/isinf() to __builtin_isnXXX() around line 34-36/
//
#include <ArduinoJson.h>

#include <SPI.h>


// #define HAS_MSL
// #define HAS_SIG1
// #define HAS_SIG2


#ifdef  ESP32
// #   include <WiFi.h>
#   include <ESPmDNS.h>
#   include <WiFiUdp.h>
#   include "FS.h"
#   include "SPIFFS.h"

#   define trng() esp_random() /* XXX we ought to check if Wifi/BT is up - as that is required for secure numbers. */
#   define resetWatchdog() { /* not implemented  -- there is a void esp_int_wdt_init() -- but we've not found the reset. */ }

// #   include <ESP32Ticker.h>  // https://github.com/bertmelis/Ticker-esp32.git

#   ifdef WIRED_ETHERNET
      extern void eth_setup();
#   endif
#else
#   include <ESP8266WiFi.h>
#   include <ESP8266mDNS.h>
#   include <ESP8266mDNS.h>
#   include <DNSServer.h>
#   include <ESP8266WebServer.h>
#   include <FS.h>
#   define resetWatchdog() { ESP.wdtFeed(); }
#   define trng() os_random()
#endif

#define BEATFORMAT     "%012lu" // hard-coded - it is part of the HMAC */
#define MAX_BEAT       16


void mqtt_callback(char* topic, byte * payload_theirs, unsigned int length);

class ACNodeSIG : public ACNodeBase {
private:
    typedef ACNodeBase super;
public:
    ACNodeSIG(const char * machine, const char * ssid, const char * ssid_passwd, acnode_proto_t proto = PROTO_SIG2);
    ACNodeSIG(const char * machine = NULL, bool wired = true, acnode_proto_t proto = PROTO_SIG2);

#ifdef INPUT
    void begin(eth_board_t board = BOARD_AART, uint8_t clear_button = INPUT /* Olimex BUT1 */);
#else
    void begin(eth_board_t board = BOARD_AART, uint8_t clear_button = -1);
#endif

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

    void request_approval(const char * tag, const char * operation = NULL, const char * target = NULL, bool useCacheOk= true);

    // This function should be private - but we're calling
    // it from a C callback in the mqtt subsystem.
    //
    void process(const char * topic, const char * payload);
    void report(JsonObject & report);

    void checkClearEEPromAndCacheButtonPressed(uint8_t button);
private:
    std::list<ACSecurityHandler*> _security_handlers;
    cmd_result_t handle_cmd(ACRequest * req);
    char _lasttag[RFID_MAX_TAG_LEN];
protected:
    acnode_proto_t _proto;
    void pop();

    void reconnectMQTT();
    void mqttLoop();

};

// For use in callbacks that are from plain C
extern ACNodeSIG *_acnode;

extern void send(const char * topic, const char * payload);

#include "SIG/Beat.h"

#include "SIG/MSL.h"
#include "SIG/SIG1.h"
#include "SIG/SIG2.h"

#include "OTA.h"

#endif
