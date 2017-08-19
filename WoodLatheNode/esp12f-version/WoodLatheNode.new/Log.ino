// Simple 'tee' class - that sends all 'serial' port data also to the MQTT bus - to the 'log' topic
// if such is possible/enabled.
//
#include "Log.h"

void Log::begin(const char * mqtt_prefix, const char * logpath, int speed) {
  Serial.begin(speed);
  while (!Serial) {
    delay(100);
  }
  // On some ESP chips - it takes up to a second before the baudrate seems to kick in. We have
  // the illusion that below helps; but see different (consistent) behaviour between otherwise
  // identical barebone ESP and NodeMCU chips with identical firmware and sequential identifier.
  //
  Serial.flush();
  Serial.print("\n\n\n\n\n");
  Serial.flush();
  
  snprintf(logtopic, sizeof(logtopic), "%s/%s/%s", mqtt_prefix, logpath, nodename);
  logbuff[0] = 0; at = 0;
  return;
}

size_t Log::write(uint8_t c) {
  size_t r = Serial.write(c);

  if (c >= 32)
    logbuff[ at++ ] = c;

  if (c != '\n' && at <= sizeof(logbuff) - 1)
    return r;

  if (client.connected()) {
    logbuff[at++] = 0;
    client.publish(logtopic, logbuff);
  };
  at = 0;

  return r;
}

void debugFlash() {
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
}

