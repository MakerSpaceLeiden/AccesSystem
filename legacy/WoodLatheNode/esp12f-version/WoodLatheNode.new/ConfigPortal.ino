#include "ConfigPortal.h"

bool shouldSaveConfig = false;
// callback notifying us of the need to save config

void saveConfigCallback () {
  shouldSaveConfig = true;
}

void debugListFS(char * path)
{
  Dir dir = SPIFFS.openDir(path);
  Debug.println("SPI File System:");
  while (dir.next()) {
    String fileName = dir.fileName();
    size_t fileSize = dir.fileSize();
    Debug.printf("FS File: %s, size: %d\n", fileName.c_str(), fileSize);
  }
  Debug.printf("\n");
}


ConfigPortal::ConfigPortal() {
  
  if (SPIFFS.begin()) {
    Debug.println("SPIFFS opened ok");
  } else {
    Log.println("Dead SPIFFS - formatting it..");
    SPIFFS.format();
  }
}

void ConfigPortal::configRun() {
  Log.print("Going into AP mode config mode\n");
  signalStateToUser(STATE_BOOTING);

  WiFiManager wifiManager;
  wifiManager.setDebugOutput(0); // avoid sensitive stuff to appear needlessly.

  WiFiManagerParameter params[ json.size() ];
  for (int i = 0, JsonObject::iterator it = json.begin(); it != json.end(); ++it,i++)
  {
    const char * key = it->key;
    JsonObject val = it->value;
    bool hide =   val.containsKey("type") && val["type"] == "hide";
    
    params[i] = WiFiManagerParameter(key, val["descr"], hide ? "" : json[key]["value"]  , (long) val["size"]);
    wifiManager.addParameter(&params[i]);
  };
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // wifiManager.autoConnect();
  String ssid = "ACNode CNF " + WiFi.macAddress();
  signalStateToUser(STATE_CONFIG);
  if (!wifiManager.startConfigPortal(ssid.c_str()))
  {
    signalStateToUser(STATE_ERROR);
    Log.println("failed to connect and hit timeout - rebooting");
    delay(1000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  if (shouldSaveConfig) {
    Debug.println("We got stuff to save!");

    DynamicJsonBuffer jsonBuffer;
    JsonObject& jsonToWrite = jsonBuffer.createObject();
    for (int i = 0, JsonObject::iterator it = json.begin(); it != json.end(); ++it, i++)
    {
      const char * key = it->key;
      JsonObject val = it->value;
      const char * str = custom_mqtt_server.getValue();;
      if (!str) str = "";        

      val["value"] = str;
      jsonToWrite[key] = val["value"];
     }

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Log.println("failed to open config file for writing");
    }

    // This will contain things like passwords in the clear
    // json.prettyPrintTo(Debug);

    json.printTo(configFile);
    configFile.close();
    configLoad();
  }
}

void ConfigPortal::configLoad() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Log.println("No JSON config file - odd");
    return;
  };

  Debug.println("opening config file");
  size_t size = configFile.size();
  std::unique_ptr<char[]> buf(new char[size]);

  configFile.readBytes(buf.get(), size);
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());
  if (!json.success()) {
    Log.println("JSON invalid - ignored.");
    // We could consider a SPIFFS.format() -- as now automatically done
    // during a failing SPIFFS open in configBegin().
    return;
  }
  
  updateConfig(json);
}
