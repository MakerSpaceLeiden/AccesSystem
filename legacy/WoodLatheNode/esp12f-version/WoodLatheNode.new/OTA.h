#ifndef _OTA_H
#define _OTA_H

#include "MakerSpaceMQTT.h"

#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

#include "Signaling.h"

class OTA : public ACNode {
  private:
    const char ota_password[32];
  public:
    OTA(void);
    void loop();
};

#endif
