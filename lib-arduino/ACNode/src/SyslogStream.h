#include <MakerSpaceMQTT.h>
#include <Print.h>

#ifndef _H_SyslogStream
#define _H_SyslogStream

class SyslogStream : public ACLog {
  public:
    const char * name() { return "SyslogStream"; }
    SyslogStream(const uint16_t syslogPort = 514) : _syslogPort(syslogPort) {};
    void setPort(uint16_t port) { _syslogPort = port; }
    void setDestination(const char * dest) { _dest = dest; }
    void setRaw(bool raw) { _raw = raw; }
    virtual size_t write(uint8_t c);
  private:
    const char * _dest;
    uint16_t _syslogPort;
    char logbuff[1024];
    size_t at = 0;
    bool _raw;
  protected:
};
#endif

