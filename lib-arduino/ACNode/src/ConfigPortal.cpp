#include <ACNode-private.h>
#include "ConfigPortal.h"
#ifdef CONFIGAP

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

void debugListFS(const char * path)
{
#ifdef  ESP32
  fs::FS fs = SPIFFS;

  Debug.printf("Listing directory: %s\n", path);
  File root = fs.open(path);
  if (!root) {
    Debug.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Debug.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Debug.print("  DIR : ");
      Debug.println(file.name());
      debugListFS(file.name()); // WARNING -- recursive
    } else {
      Debug.print("  FILE: ");
      Debug.print(file.name());
      Debug.print("  SIZE: ");
      Debug.println(file.size());
    }
    file = root.openNextFile();
  }
#else
  Dir dir = SPIFFS.openDir(path);
  Debug.println("SPI File System:");
  while (dir.next()) {
    String fileName = dir.fileName();
    size_t fileSize = dir.fileSize();
    Debug.printf("FS File: %s, size: %d\n", fileName.c_str(), fileSize);
  }
#endif
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  shouldSaveConfig = true;
}

void configPortal() {
  Log.print("Going into AP mode config mode\n");
  WiFiManager wifiManager;
  wifiManager.setDebugOutput(255); // avoid sensitive stuff to appear needlessly.

  char mqtt_port_buff[5];
  char passwd_buff[MAX_NAME];
  snprintf(mqtt_port_buff, sizeof(mqtt_port_buff), "%d", _acnode->mqtt_port);
  passwd_buff[0] = 0; // force user to (re)set the password; rather than reveal anything.

  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", 
	_acnode->mqtt_server, sizeof(_acnode->mqtt_server));
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port_buff, sizeof(mqtt_port_buff));
  WiFiManagerParameter custom_node("node", "node name", _acnode->moi, sizeof(_acnode->moi));
  WiFiManagerParameter custom_machine("machine", "machine", _acnode->machine, sizeof(_acnode->machine));
  WiFiManagerParameter custom_prefix("topic_prefix", "topix prefix", _acnode->mqtt_topic_prefix, sizeof(_acnode->mqtt_topic_prefix));
  WiFiManagerParameter custom_passwd("passwd", "shared secret", passwd_buff, sizeof(passwd_buff));
  WiFiManagerParameter custom_master("master", "master node", _acnode->master, sizeof(_acnode->master));
  WiFiManagerParameter custom_logpath("logpath", "logpath", _acnode->logpath, sizeof(_acnode->logpath));

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
    Log.println("failed to connect and hit timeout - rebooting");
    delay(1000);
    //reset and try again, or maybe put it to deep sleep
#ifdef  ESP32
    esp_restart();
#else
    ESP.reset();
#endif
    delay(5000);
  }

  if (shouldSaveConfig) {
    Log.println("We got stuff to save!");

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
      Log.println("failed to open config file for writing");
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
  Debug.println("opening config file");
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
    const char * str = json[(v)]; \
    if (str) { strncpy((d),str,sizeof((d))); defined++; }; \
    Debug.printf("%s=\"%s\" ==> %s\n", v, str ? (strcmp(v,"passwd") ? str : "****") : "\\0",  (strcmp(v,"passwd") ? d : "****"));\
  }
  JSONR(_acnode->mqtt_server, "mqtt_server");
  JSONR(tmp_port, "mqtt_port");
  JSONR(_acnode->moi, "moi");
  JSONR(_acnode->mqtt_topic_prefix, "prefix");
//  JSONR(_acnode->passwd, "passwd");
  JSONR(_acnode->logpath, "logpath");
  JSONR(_acnode->master, "master");
  JSONR(_acnode->machine, "machine");

  int p = atoi(tmp_port);
  if (p == 0) p = MQTT_DEFAULT_PORT;
  if (p < 65564) _acnode->mqtt_port = p;

  return defined == 8;
}
#endif

