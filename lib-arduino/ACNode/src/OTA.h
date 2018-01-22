#ifndef _H_OTA
#define _H_OTA

#include <ACBase.h>

class OTA: public ACBase
{
  public:
    OTA(const char * password);
    void loop();
    void begin();
  protected:
	const char * _ota_password;
};
#endif
