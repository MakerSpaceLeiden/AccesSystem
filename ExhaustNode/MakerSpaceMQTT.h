#pragma once

#include <PubSubClient.h>        // https://github.com/knolleary/

// MQTT limits - which are partly ESP chip rather than protocol specific.
// MQTT limits - which are partly ESP chip rather than protocol specific.
#define MAX_NAME       24
#define MAX_TOPIC      64
#define MAX_MSG        (MQTT_MAX_PACKET_SIZE - 32)
#define MAX_TAG_LEN    10/* Based on the MFRC522 header */
#define BEATFORMAT     "%012u" // hard-coded - it is part of the HMAC */
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
