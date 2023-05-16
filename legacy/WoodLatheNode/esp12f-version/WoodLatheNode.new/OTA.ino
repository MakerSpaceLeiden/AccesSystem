#include "OTA.h"


void OTA:OTA() {
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(nodename);

  // We currenly hardcode this - as to not allow an 'easy' bypass by
  // means of the captive portal activation & subsequent change.
  //
  ArduinoOTA.setPassword(ota_password);

  ArduinoOTA.onStart([]() {
    Log.println("OTA process started.");
    signalStateToUser(STATE_UPDATE);
  });
  ArduinoOTA.onEnd([]() {
    Log.println("OTA process completed. Resetting.");
    signalStateToUser(STATE_BOOTING);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Log.printf("%c%c%c%cProgress: %u%% ", 27, '[', '1', 'G', (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    signalStateToUser(STATE_ERROR);
    Log.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Log.println("OTA: Auth failed");
    else if (error == OTA_BEGIN_ERROR) Log.println("OTA: Begin failed");
    else if (error == OTA_CONNECT_ERROR) Log.println("OTA: Connect failed");
    else if (error == OTA_RECEIVE_ERROR) Log.println("OTA: Receive failed");
    else if (error == OTA_END_ERROR) Log.println("OTA: End failed");
    else {
      Log.print("OTA: Error: ");
      Log.println(error);
    };
  });
  ArduinoOTA.begin();
  Log.println("OTA Enabled");
}

void OTA::loop() {
  super::loop();
  ArduinoOTA.handle();
}


