#include <MakerSpaceMQTT.h>
#include <Print.h>

#ifndef _H_SyslogStream
#define _H_SyslogStream

class SyslogStream : public Print {
  public:
    SyslogStream(const uint16_t syslogPort = 514) : _syslogPort(syslogPort) {};
    virtual size_t write(uint8_t c);
  private:
    uint16_t _syslogPort;
    char logbuff[256];
    size_t at;
  protected:
};
#endif

