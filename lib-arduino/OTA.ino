#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

void configureOTA() {
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(moi);

  // We currenly hardcode this - as to not allow an 'easy' bypass by
  // means of the captive portal activation & subsequent change.
  //
  ArduinoOTA.setPassword((const char *)OTA_PASSWD);

  ArduinoOTA.onStart([]() {
    Log.println("OTA process started.");
    setGreenLED(LED_SLOW);
    setOrangeLED(LED_SLOW);
  });
  ArduinoOTA.onEnd([]() {
    Log.println("OTA process completed. Resetting.");
    setGreenLED(LED_OFF);
    setOrangeLED(LED_ON);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Log.printf("%c%c%c%cProgress: %u%% ", 27, '[', '1', 'G', (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    setGreenLED(LED_FAST);
    setOrangeLED(LED_FAST);
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

void otaLoop() {
  ArduinoOTA.handle();
}

