// Simple 'tee' class - that sends all 'serial' port data also to the Syslog and/or MQTT bus - 
// to the 'log' topic if such is possible/enabled.
//
// XXX should refactor in a generic buffered 'add a Stream' class and then
// make the various destinations classes in their own right you can 'add' to the T.
//
//
#include <ACNode.h>

#include "TLog.h"
TLog Log;

TLog::TLog() {}

void TLog::begin(const char * prefix, int speed, uint16_t syslogPort)
{
  _syslogPort = syslogPort;

  if (speed) {
	_doSerial = true;
	Serial.begin(speed);
	while (!Serial) 
    		delay(100);
#ifdef GDBSTUB_H
	Serial.println("*****  GDB debugging is enabled - no serial output ****");
#endif
  };
  if(prefix) {
	_doMqtt = true;
  	snprintf(logtopic, sizeof(logtopic), "%s/%s/%s", prefix, logpath, moi);
  	logbuff[0] = 0; at = 0;
  };
  return;
}

size_t TLog::write(uint8_t c) {
  // Avoid outputting any data when we have the GDB stub included; as GDB gets
  // confused by base64 strings.
  size_t r = 1;
#ifndef GDBSTUB_H
  if (_doSerial)
	  r = Serial.write(c);
#endif

  if (c >= 32)
    logbuff[ at++ ] = c;

  if (c != '\n' && at <= sizeof(logbuff) - 1)
    return r;

  logbuff[at++] = 0;
  at = 0;

  if (_doMqtt)
	send(logtopic, logbuff);

#ifndef WIRED_ETHERNET
  if (WiFi.status() == WL_CONNECTED) 
#endif
  if (_syslogPort) {

    WiFiUDP syslog;
    if (syslog.begin(_syslogPort)) {
      syslog.beginPacket(WiFi.gatewayIP(), _syslogPort);
#ifdef  ESP32
      syslog.printf("<135>%s %s", moi, logbuff);
#else
      syslog.write("<135>");
      syslog.write(moi);
      syslog.write(" ");
      syslog.write(logbuff);
#endif
      syslog.endPacket();
    };
  };

  return r;
}

void debugFlash() {
#ifdef  ESP32
  // not implemented.
  Debug.println("debugFlash() not implemented.");
#else
  uint32_t realSize = ESP.getFlashChipRealSize();
  uint32_t ideSize = ESP.getFlashChipSize();
  FlashMode_t ideMode = ESP.getFlashChipMode();

  Debug.printf("Flash real id:   %08X\n", ESP.getFlashChipId());
  Debug.printf("Flash real size: %u bytes (%u MB)\n\n", realSize, realSize >> 20);

  Debug.printf("Flash ide  size: %u bytes\n", ideSize);
  Debug.printf("Flash ide speed: %u\n", ESP.getFlashChipSpeed());
  Debug.printf("Flash ide mode:  %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));

  if (ideSize != realSize) {
    Debug.println("Flash Chip configuration wrong!\n");
  } else {
    Debug.println("Flash Chip configuration ok.\n");
  }
#endif
}

