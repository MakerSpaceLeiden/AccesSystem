// Simple 'tee' class - that sends all 'serial' port data also to the Syslog and/or MQTT bus - to the 'log' topic
// if such is possible/enabled.
//
#include <WiFiUdp.h>

#include "Log.h"

const uint16_t syslogPort = 514;

void Log::begin(const char * prefix, int speed) {
  Serial.begin(speed);
  while (!Serial) {
    delay(100);
  }
  Serial.print("\n\n\n\n\nstart\n\n");
  snprintf(logtopic, sizeof(logtopic), "%s/%s/%s", prefix, logpath, moi);
  logbuff[0] = 0; at = 0;

  return;
}

size_t Log::write(uint8_t c) {
  // Avoid outputting any data when we have the GDB stub included; as GDB gets
  // confused by base64 strings.
#ifndef GDBSTUB_H
  size_t r = Serial.write(c);
#endif

  if (c >= 32)
    logbuff[ at++ ] = c;

  if (c != '\n' && at <= sizeof(logbuff) - 1)
    return r;

  logbuff[at++] = 0;
  at = 0;

  send(logtopic, logbuff);

  if (WiFi.status() == WL_CONNECTED) {

    WiFiUDP syslog;
    if (syslog.begin(syslogPort)) {
      syslog.beginPacket(WiFi.gatewayIP(), syslogPort);
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

