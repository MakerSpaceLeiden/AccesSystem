#ifndef _OTA_H
#define _OTA_H

#include "MakerSpaceMQTT.h"
#include "Signaling.h"

#if 0
class OTA : public ACNode {
  public:
    OTA();
    loop();
  private:
    const char ota_password[32];
};
#endif

extern void configureOTA();
extern void otaLoop();


#endif
