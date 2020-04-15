#pragma once

#ifdef  ESP32
#   include <WiFi.h>
#   include <ESPmDNS.h>
#   include <WiFiUdp.h>
#   include "FS.h"
#   include "SPIFFS.h"

#   define trng() esp_random() /* XXX we ought to check if Wifi/BT is up - as that is required for secure numbers. */
#   define resetWatchdog() { /* not implemented  -- there is a void esp_int_wdt_init() -- but we've not found the reset. */ }

#   include <ESP32Ticker.h>  // https://github.com/bertmelis/Ticker-esp32.git

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
#   define trng() ((uint32_t)  ESP8266_DREG(0x20E44)) /* XXX we ought to check if Wifi/BT is up - as that is required for secure numbers. */
#   include <Ticker.h>
#endif

#include <ArduinoOTA.h>
#include <WiFiManager.h>
#include <PubSubClient.h>        // https://github.com/knolleary/

#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>        // https://github.com/knolleary/
#include <base64.hpp>


// ArduinoJSON library -- from https://github.com/bblanchon/ArduinoJson - installed th
//
// Depending on your version - if you get an osbcure error in
// .../ArduinoJson/Polyfills/isNaN.hpp and isInfinity.hpp - then
// isnan()/isinf() to __builtin_isnXXX() around line 34-36/
//
#include <ArduinoJson.h>

#include <SPI.h>
#ifndef SHA256_BLOCK_SIZE
#define SHA256_BLOCK_SIZE (32)
#endif

#ifndef HASH_LENGTH 
#define HASH_LENGTH SHA256_BLOCK_SIZE
#endif

// MQTT limits - which are partly ESP chip rather than protocol specific.
// MQTT limits - which are partly ESP chip rather than protocol specific.
#define MAX_NAME       24
#define MAX_TOPIC      64
#define MAX_MSG        (MQTT_MAX_PACKET_SIZE - 32)
#define MAX_TAG_LEN    10/* Based on the MFRC522 header */
#define BEATFORMAT     "%012lu" // hard-coded - it is part of the HMAC */
#define MAX_BEAT       16

#ifndef MQTT_DEFAULT_PORT
#define MQTT_DEFAULT_PORT (1883)
#endif

extern char mqtt_server[34];
extern uint16_t mqtt_port;

// MQTT topics are constructed from <prefix> / <dest> / <sender>
//
extern char mqtt_topic_prefix[MAX_TOPIC];
extern char moi[MAX_NAME];    // Name of the sender
extern char machine[MAX_NAME];
extern char master[MAX_NAME];    // Destination for commands
extern char logpath[MAX_NAME];       // Destination for human readable text/logging info.
extern char passwd[MAX_NAME];

#include "SIG1.h"
#include "SIG2.h"

// Forward declarations..
//
extern void configureMQTT();
extern void send(const char * topic, const char * payload);
extern void mqttLoop();
bool sig2_active();

