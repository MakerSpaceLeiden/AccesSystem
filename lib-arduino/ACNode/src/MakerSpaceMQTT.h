#pragma once

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

#include <Ticker.h>

#include <ArduinoOTA.h>
#include <PubSubClient.h>        // https://github.com/knolleary/

#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>        // https://github.com/knolleary/

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

#ifndef MQTT_SERVER
#define MQTT_SERVER 	 "spacebus.makerspaceleiden.nl"
#endif

#ifndef MQTT_TOPIC_PREFIX 
#define MQTT_TOPIC_PREFIX "test"
#endif

#ifndef MQTT_TOPIC_LOG
#define MQTT_TOPIC_LOG 	   "log"
#endif

#ifndef MQTT_TOPIC_MASTER
#define MQTT_TOPIC_MASTER "master"
#endif

#ifndef MQTT_DEFAULT_PORT
#define MQTT_DEFAULT_PORT (1883)
#endif

#if MQTT_MAX_PACKET_SIZE < 512
#error "You will need to increase te MQTT_MAX_PACKET_SIZE size a bit in PubSubClient.h"
#endif

// MQTT limits - which are partly ESP chip rather than protocol specific.
// MQTT limits - which are partly ESP chip rather than protocol specific.
#define MAX_HOST       64
#define MAX_NAME       24
#define MAX_TOPIC      ((MAX_NAME +1) * 3  + 1)
#define MAX_MSG        (512)
#define BEATFORMAT     "%012lu" // hard-coded - it is part of the HMAC */
#define MAX_BEAT       16

#define RFID_MAX_TAG_LEN 48
