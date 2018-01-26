// Simple 'tee' class - that sends all 'serial' port data also to the Syslog and/or MQTT bus - 
// to the 'log' topic if such is possible/enabled.
//
// XXX should refactor in a generic buffered 'add a Stream' class and then
// make the various destinations classes in their own right you can 'add' to the T.
//
//
#include <ACNode.h>
#include "SyslogStream.h"

size_t SyslogStream::write(uint8_t c) {
  if (c >= 32 && c < 128) 
    logbuff[ at++ ] = c;

  if (c != '\n' && at <= sizeof(logbuff) - 1)
    return 1;

  logbuff[at++] = 0;
  at = 0;

   if (node.isConnected()) {
    WiFiUDP syslog;
    if (syslog.begin(_syslogPort)) {
      syslog.beginPacket(WiFi.gatewayIP(), _syslogPort);
      syslog.printf("<135> NoTimeStamp %s %s", moi, logbuff);
      syslog.endPacket();
    };
};

  return 1;
}

