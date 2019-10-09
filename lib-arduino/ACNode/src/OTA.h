#ifndef _H_OTA
#define _H_OTA

#include <ACBase.h>

class OTA: public ACBase
{
  public:
    const char * name() { return "OTA"; }
    OTA(const char * password);
    void loop();
    void begin();
    void report(JsonObject& report);
  protected:
	const char * _ota_password;
};
#endif
