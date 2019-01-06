#include <MakerSpaceMQTT.h>
#include <Print.h>

#ifndef _H_TelnetSerialStream
#define _H_TelnetSerialStream

#ifndef MAX_SERIAL_TELNET_CLIENTS
#define MAX_SERIAL_TELNET_CLIENTS (4)
#endif

class TelnetSerialStream : public ACLog {
  public:
    const char * name() { return "TelnetSerialStream"; }
    TelnetSerialStream(const uint16_t telnetPort = 23 ) : _telnetPort(telnetPort) {};
    virtual size_t write(uint8_t c);
    virtual void begin();
    virtual void loop();
    virtual void stop();

  private:
    uint16_t _telnetPort;
    WiFiServer * _server = NULL;
    WiFiClient _serverClients[MAX_SERIAL_TELNET_CLIENTS];
  protected:
};
#endif

