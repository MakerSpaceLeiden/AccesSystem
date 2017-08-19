
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>        // https://github.com/knolleary/

// ArduinoJSON library -- from https://github.com/bblanchon/ArduinoJson - installed th
//
// Depending on your version - if you get an osbcure error in
// .../ArduinoJson/Polyfills/isNaN.hpp and isInfinity.hpp - then
// isnan()/isinf() to __builtin_isnXXX() around line 34-36/
//
#include <ArduinoJson.h>

#include <SPI.h>
#include <FS.h>

#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

#include <SPI.h>
#include <FS.h>

//flag for saving data
bool shouldSaveConfig = false;

void  configBegin() {
  if (SPIFFS.begin()) {
    Debug.println("SPIFFS opened ok");
  } else {
    Log.println("Dead SPIFFS - formatting it..");
    SPIFFS.format();
  }
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

//callback notifying us of the need to save config
void saveConfigCallback () {
  shouldSaveConfig = true;
}

void configPortal() {
  Log.print("Going into AP mode config mode\n");
  setGreenLED(LED_OFF);
  setOrangeLED(LED_FAST);

  WiFiManager wifiManager;
  wifiManager.setDebugOutput(0); // avoid sensitive stuff to appear needlessly.

  char mqtt_port_buff[5];
  char passwd_buff[MAX_NAME];
  snprintf(mqtt_port_buff, sizeof(mqtt_port_buff), "%d", mqtt_port);
  passwd_buff[0] = 0; // force user to (re)set the password; rather than reveal anything.

  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, sizeof(mqtt_server));
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port_buff, sizeof(mqtt_port_buff));
  WiFiManagerParameter custom_node("node", "node name", moi, sizeof(moi));
  WiFiManagerParameter custom_machine("machine", "machine", machine, sizeof(machine));
  WiFiManagerParameter custom_prefix("topic_prefix", "topix prefix", mqtt_topic_prefix, sizeof(mqtt_topic_prefix));
  WiFiManagerParameter custom_passwd("passwd", "shared secret", passwd_buff, sizeof(passwd_buff));
  WiFiManagerParameter custom_master("master", "master node", master, sizeof(master));
  WiFiManagerParameter custom_logpath("logpath", "logpath", logpath, sizeof(logpath));

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_logpath);
  wifiManager.addParameter(&custom_prefix);

  wifiManager.addParameter(&custom_node);
  wifiManager.addParameter(&custom_machine);

  wifiManager.addParameter(&custom_master);
  wifiManager.addParameter(&custom_passwd);

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // wifiManager.autoConnect();
  String ssid = "ACNode CNF " + WiFi.macAddress();
  if (!wifiManager.startConfigPortal(ssid.c_str()))
  {
    Serial.println("failed to connect and hit timeout - rebooting");
    delay(1000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  if (shouldSaveConfig) {
    Serial.println("We got stuff to save!");

    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();

    json["mqtt_server"] = custom_mqtt_server.getValue();
    json["mqtt_port"] = custom_mqtt_port.getValue();
    json["moi"] = custom_node.getValue();
    json["machine"] = custom_machine.getValue();
    json["master"] = custom_master.getValue();
    json["prefix"] = custom_prefix.getValue();
    json["passwd"] = custom_passwd.getValue();
    json["logpath"] = custom_logpath.getValue();

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    // This will contain things like passwords in the clear
    // json.prettyPrintTo(Serial);

    json.printTo(configFile);
    configFile.close();
  }
}

int configLoad() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Log.println("No JSON config file - odd");
    return 0;
  }
  Serial.println("opening config file");
  size_t size = configFile.size();
  std::unique_ptr<char[]> buf(new char[size]);

  configFile.readBytes(buf.get(), size);
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());
  if (!json.success()) {
    Log.println("JSON invalid");
    return 0;
  };
  char tmp_port[32];
  int defined = 0;

#define JSONR(d,v) { \
    const char * str = json[v]; \
    if (str) { strncpy(d,str,sizeof(d)); defined++; }; \
    Debug.printf("%s=\"%s\" ==> %s\n", v, str ? (strcmp(v,"passwd") ? str : "****") : "\\0",  (strcmp(v,"passwd") ? d : "****"));\
  }
  JSONR(mqtt_server, "mqtt_server");
  JSONR(tmp_port, "mqtt_port");
  JSONR(moi, "moi");
  JSONR(mqtt_topic_prefix, "prefix");
  JSONR(passwd, "passwd");
  JSONR(logpath, "logpath");
  JSONR(master, "master");
  JSONR(machine, "machine");

  int p = atoi(tmp_port);
  if (p == 0) p = MQTT_DEFAULT_PORT;
  if (p < 65564) mqtt_port = p;

  return defined == 8;
}
