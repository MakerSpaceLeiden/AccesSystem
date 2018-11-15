// Simple 'tee' class - that sends all 'serial' port data also to the Syslog and/or MQTT bus - 
// to the 'log' topic if such is possible/enabled.
//
// XXX should refactor in a generic buffered 'add a Stream' class and then
// make the various destinations classes in their own right you can 'add' to the T.
//
//
#include <ACNode-private.h>
#include <MqttLogStream.h>

#include "time.h"

MqttLogStream::MqttLogStream() {
  _logbuff[0] = 0; _at = 0;
  return;
}

void MqttLogStream::begin() {
  snprintf(_logtopic, sizeof(_logtopic), "%s/%s/%s",
	_acnode->mqtt_topic_prefix, _acnode->logpath, _acnode->moi);
  Debug.printf("Sending output to logtopic %s\n", _logtopic);
}

size_t MqttLogStream::write(uint8_t c) {
    if (_at >= sizeof(_logbuff)-1) {
        Serial.println("Purged logbuffer");
        _at = 0;
    };

  if (c  >= 32)
    _logbuff[ _at++ ] = c;

  if (c == '\n' || _at >= sizeof(_logbuff) - 1) {
    _logbuff[_at] = 0;
    _at = 0;

     char buff[sizeof(_logbuff)];
#if 0 // This seems to take seconds. So disbaling for now.
     struct tm timeinfo;
     getLocalTime(&timeinfo);
    char * tstr = asctime(&timeinfo);
    tstr[strlen(tstr)-1] = '\0';

     snprintf(buff,sizeof(buff),"%s %s %s", tstr , _acnode->moi, _logbuff);
#else
     snprintf(buff,sizeof(buff),"%s %s", _acnode->moi, _logbuff);
#endif
     if (_acnode->isUp())
	     _acnode->send(_logtopic, buff, true);
     // else silently drop logging data when the bus is down.
  }
  return 1;
}
