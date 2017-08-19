#ifndef _H_CONFIGPORTAL
#define _H_CONFIGPORTAL


#include "MakerSpaceMQTT.h"



class ConfigPortal : ACNode {
 public:  
   ConfigPortal(); 
   void configRun();
   void configLoad();
};

extern void debugListFS(char * path);
#endif

