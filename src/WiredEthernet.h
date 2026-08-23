#include <ACBaseNode.h>

#ifndef _H_WIRED_ETHERNET
#define _H_WIRED_ETHERNET

#ifdef ESP32
#include <WiFi.h>
extern bool eth_connected();
extern void WiFiEvent(WiFiEvent_t event);
extern void WiFiEventLoop();

#endif

#endif

