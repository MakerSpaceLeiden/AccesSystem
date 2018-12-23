#ifdef ESP32

#include <Cache.h>
#include <string.h>
#include <Arduino.h>
#include "FS.h"
#include "SPIFFS.h"

#define CACHE_DIR_PREFIX "/uid-"

unsigned long cacheMiss = 0;
unsigned long cacheHit = 0;

static String uid2path(const char * tag) {
  String path = CACHE_DIR_PREFIX;
 
  byte crc = 0;
  for(const char * p = tag; p && *p; p++)
	crc = (( 37 * crc ) + *p) & 0xFF;

  return CACHE_DIR_PREFIX + String(crc,HEX) + "/" + tag;
}

void prepareCache(bool wipe) {
  Serial.println(wipe ? "Resetting cache" : "Cache preparing.");
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS mount after formatting failed.");
    return;
  };

  if (wipe) for (int i = 0; i < 255; i++) {
    String dirName = CACHE_DIR_PREFIX + String(i,HEX);
    if (!SPIFFS.exists(dirName)) 
    	SPIFFS.mkdir(dirName);

      File dir = SPIFFS.open(dirName);
      File file = dir.openNextFile();
      while(file) {
  	String path = dirName + "/" + file.name();
  
  	file.close();
        SPIFFS.remove(path);

	file = file.openNextFile();
      };
      dir.close();
  };

  Serial.println("Cache ready.");
};

void setCache(const char * tag, bool ok, unsigned long beatCounter) {
  String path = uid2path(tag) + ".lastOK";
  if (ok) {
    File f = SPIFFS.open(path, "w");
    f.println(beatCounter);
    f.close();
  } else {
    SPIFFS.remove(path);
  }
};

bool checkCache(const char * tag) {
  String path = uid2path(tag) + ".lastOK";
  bool present = SPIFFS.exists(path);
  if (present) cacheHit++; else cacheMiss++;
  return present;
};
#else
void prepareCache(bool wipe) { return; }
void setCache(const char * tag, bool ok, unsigned long beatCounter) { return; };
bool checkCache(const char * tag) { return false; };
#endif

