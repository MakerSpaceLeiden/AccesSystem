#include <MakerSpaceMQTT.h>
#include <Print.h>

#ifndef _H_MqttLogStream
#define _H_MqttLogStream

class MqttLogStream: public ACLog {
  public:
    const char * name() { return "MqttLogStream"; }
    MqttLogStream();
    void begin();
    virtual size_t write(uint8_t c); 
  private:
    char _logbuff[256], _logtopic[256];
    size_t _at;
  protected:
};
#endif
