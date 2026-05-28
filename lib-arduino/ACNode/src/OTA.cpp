#include <ACBaseNode.h>
#include <OTA.h>
#include "util/part.h"

OTA::OTA(const char * password) : _ota_password_hash(password) {
    if (!_ota_password_hash) {
        Log.println("**** WARNING -- NO OTA PASSWORD SET *****");
	return;
    };

    ArduinoOTA.setPasswordHash(_ota_password_hash);

    size_t l = strlen(_ota_password_hash);

    if (l != 32 && l != 64) {
            safestrcpy(_ota_masked_password_hash,"****");
	    return;
    };

    strncpy(_ota_masked_password_hash, _ota_password_hash, 3);
    strncpy(_ota_masked_password_hash+3, "...", 4);
    strncpy(_ota_masked_password_hash+6, _ota_password_hash+ l - 3, 3);
} 

void OTA::begin() {
    ArduinoOTA.setHostname((_acnodebase->moi[0]) ? _acnodebase->moi : "unset-acnode");
    
    ArduinoOTA.onStart([]() {
        Log.println("OTA process started (trusting though - not wiping private keys).");

        Serial.print("Progress: 0%");
#if 0
        Log.stop();
        Debug.stop();
	_acnodebase->webServer()->stop();
#endif
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("..100% Done");
        Log.println("OTA process completed. Resetting.");
        ESP.restart(); // ????? reboot sometimes not working ??
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

void OTA::report(JsonObject & report) {
    JsonObject ota = report["ota"].add<JsonObject>();
    ota["enabled"] = true;
    ota["ota_hash"] = passwdType();
    ota["ota_pass"] = _ota_masked_password_hash;
}

const char * OTA::passwdType() {
    size_t l = _ota_password_hash ? strlen(_ota_password_hash) : 0;

    if (l == 0)
	return "none";
    if (l == 64)
	return "sha256";
    if (l == 32)
        return "md5";
    return "plain";
}

void OTA::loop() {
    ArduinoOTA.handle();
}

OTAWithDisplay::OTAWithDisplay(const char * password, Display *d, const char * hostname) : OTA(password), _display(d), _hostname(hostname) {};

void OTAWithDisplay::begin() {
    const char * name = ((_hostname != NULL) && (_hostname[0] != '\0')) ? _hostname : "unset-acnode";
    ArduinoOTA.setHostname(name);

    ArduinoOTA.onStart([&]() {
        if (_ota_ok_cb && !_ota_ok_cb()) {
            Log.printf("CRITICAL: Rejected OTA updated as machine is currently in use\n");
            
            // Until our pull requests makes it through - there appears to be no reliable
            // way to abort an OTA upload forcefully. An .end() gets reset by the next
            // valid UDP packet arriving. So in onProgress we keep messing with the state
            // until it positively errors out.
            ///
            _otaOK = false;
            for(int i = 0; i < 100; i++) { ArduinoOTA.end(); delay(20); };
            return;
        };
        if (_display) {
            _display->updateDisplay("OTA","","",true);
            _display->updateDisplayStateMsg("updating firmware",0);
            _display->updateDisplayProgressbar(0,true);
            _display->setDisplayScreensaver(false);
        };
        
        if (strstr(_acnodebase->moi,"test"))
            Log.println("OTA process started (Not wiping private keys in test code).");
        else {
            Log.println("OTA process started -- wiping private keys.");
            if (_pre_secrets_cb)
                _pre_secrets_cb(); // wipe_eeprom();
            Log.println("Secrets/Keys wiped. Do not forget to repair or reset the TOFU on the server.");
        };
        Serial.print("Progress: 0%");
        // Log.stop();
        // Debug.stop();
    });
    ArduinoOTA.onEnd([&]() {
        if (_otaOK) {
            if (_display) {
                _display->updateDisplayStateMsg("ok, rebooting",1);
                _display->updateDisplayProgressbar(100);
            };
            Serial.println("..100% Done");
            Log.println("OTA process completed, rebooting");
        } else {
            Log.println("Ignoring an OTA end (as we are trying to reject the OTA");
        };
        _otaOK = true;
    });
    ArduinoOTA.onProgress([&](unsigned int progress, unsigned int total) {
        if (!_otaOK) {
            Log.println("Ignoring OTA update; trying to block it.");
            for(int i = 0; i < 100; i++) { ArduinoOTA.end(); delay(20); };
            return;
        };
        static int lp = 0;
        int p = (int)(30. * progress / total + 0.5);
        if (p != lp) {
            lp = p;
            int perc = (progress / (total / 100));
            Serial.printf("..%u%%", perc);
            if (_display)
                _display->updateDisplayProgressbar(perc);
        };
    });
    ArduinoOTA.onError([&](ota_error_t error) {
        String cause = String(error);
        
        if (error == OTA_AUTH_ERROR) cause = "OTA: Auth failed";
        else if (error == OTA_BEGIN_ERROR) cause = "OTA: Begin failed";
        else if (error == OTA_CONNECT_ERROR) cause = "OTA: Connect failed";
        else if (error == OTA_RECEIVE_ERROR) cause = "OTA: Receive failed";
        else if (error == OTA_END_ERROR) cause = "OTA: End failed";
        
        Log.println("OTA Failed: " + cause);
        
        // If we did not reject the upload; then do not
        // change state; to prevent us messing with the
        // current machine state or the display.
        //
        if (_otaOK && _display) {
            _display->updateDisplay("OTA","","",true);
            _display->updateDisplayStateMsg("update failed",0);
            _display->updateDisplayStateMsg(cause.c_str(),1);
            _display->updateDisplayProgressbar(0, true);
        };
        
        _otaOK = true;
        ArduinoOTA.begin();
    });


    ArduinoOTA.begin();
    _otaOK = true;

    Log.printf("OTA Enabled on %s:%d %s - password-hash: %s\n",
               ArduinoOTA.getHostname().c_str(),
               OTA_PORT,
               ArduinoOTA.getPartitionLabel().c_str(),
               _ota_password_hash ? "set" : "UNSET"
               );
}

void OTADeck::render_pane(bool refresh) {
    if (!_display)
	return;

    if (!refresh)
        return;
    
    _display->print_centred("OTA");
    _display->printf("Host: %s\n",ArduinoOTA.getHostname().c_str());
    _display->printf("Port: %d\n",OTA_PORT);
    _display->printf("Slce: %s\n",currentPartition().c_str());
};
