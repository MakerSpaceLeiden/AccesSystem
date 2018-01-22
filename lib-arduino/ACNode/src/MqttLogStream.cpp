// Simple 'tee' class - that sends all 'serial' port data also to the Syslog and/or MQTT bus - 
// to the 'log' topic if such is possible/enabled.
//
// XXX should refactor in a generic buffered 'add a Stream' class and then
// make the various destinations classes in their own right you can 'add' to the T.
//
//
#include <ACNode.h>
#include <MqttLogStream.h>

MqttLogStream::MqttLogStream(const char prefix) {
  snprintf(_logtopic, sizeof(logtopic), "%s/%s/%s", prefix, logpath, moi);
  _logbuff[0] = 0; _at = 0;
  return;
}

size_t MqttLogStream::write(uint8_t c) {
  if (c  >= 32) 
    _logbuff[ _at++ ] = c;

  if (c != '\n' && _at <= sizeof(_logbuff) - 1)
    return 1

  logbuff[_at++] = 0;
  _at = 0;

  send(_logtopic, _logbuff);
  return 1;
}

