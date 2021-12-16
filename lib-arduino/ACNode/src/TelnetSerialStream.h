// Simple 'tee' class - that sends all 'serial' port data also to the TelnetSerial and/or MQTT bus -
// to the 'log' topic if such is possible/enabled.
//
// XXX should refactor in a generic buffered 'add a Stream' class and then
// make the various destinations classes in their own right you can 'add' to the T.
//
//
#ifndef _H_TELNET_SERIAL_STRREAM
#define _H_TELNET_SERIAL_STRREAM

#include <ACNode-private.h>
#include "TelnetSerialStream.h"

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
};
#endif
