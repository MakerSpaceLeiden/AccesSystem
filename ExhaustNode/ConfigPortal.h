#ifndef _H_CONFIGPORTAL
#define _H_CONFIGPORTAL


#include "MakerSpaceMQTT.h"


#if 0
class ConfigPortal : ACNode {
  public:
    ConfigPortal();
    void configRun();
    void configLoad();
};
#endif

extern void debugListFS(char * path);
extern void  configBegin();
extern int configLoad();
extern void configPortal();
extern void configRun();
#endif

