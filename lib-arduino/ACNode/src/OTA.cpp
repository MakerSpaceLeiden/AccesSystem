#include <ACNode-private.h>
#include <OTA.h>

OTA::OTA(const char * password) : _ota_password(password) {};

void OTA::begin() {
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname((_acnode->moi && _acnode->moi[0]) ? _acnode->moi : "unset-acnode");

  if (_ota_password) 
	  ArduinoOTA.setPassword(_ota_password);
  else 
  	Log.println("**** WARNING -- NO OTA PASSWORD SET *****");

  ArduinoOTA.onStart([]() {
    Log.println("OTA process started.");
    Serial.print("Progress: 0%");
    Log.stop();
    Debug.stop();
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("..100% Done");
    Log.println("OTA process completed. Resetting.");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static int lp = 0;
    int p = (int)(10. * progress / total + 0.5);
    if (p != lp) {
	lp = p;
        Serial.printf("..%u%%", (progress / (total / 100)));
    };
  });
  ArduinoOTA.onError([](ota_error_t error) {
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
  Debug.println("OTA Enabled");
}

void OTA::loop() {
  ArduinoOTA.handle();
}
