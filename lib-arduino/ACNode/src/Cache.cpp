#ifdef ESP32

#include <Cache.h>
#include <string.h>
#include <Arduino.h>
#include "FS.h"
#include "SPIFFS.h"

#define CACHE_DIR_PREFIX "/uid-"

unsigned long cacheMiss = 0;
unsigned long cacheHit = 0;
unsigned long cachePurge = 0;
unsigned long cacheUpdate= 0;

static String uid2path(const char * tag) {
  String path = CACHE_DIR_PREFIX;
 
  byte crc = 0;
  for(const char * p = tag; p && *p; p++)
	crc = (( 37 * crc ) + *p) & 0xFF;

  return CACHE_DIR_PREFIX + String(crc,HEX) + "/" + tag;
}

void prepareCache(bool wipe) {
  Log.println(wipe ? "Resetting cache" : "Cache preparing.");
  if (!SPIFFS.begin()) {
    Log.println("Mount failed - trying to reformat");
    if (!SPIFFS.format() || !SPIFFS.begin()) {
      Log.println("SPIFFS mount after re-formatting also failed. Giving up. No caching.");
      return;
    };
  };

  if (wipe) 
	wipeCache();
  Log.println("Cache ready.");
};

void wipeCache() { 
  for (int i = 0; i < 255; i++) {
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
  Log.println("Cache wiped");
}

void setCache(const char * tag, bool ok, unsigned long beatCounter) {
  String path = uid2path(tag) + ".lastOK";
  if (ok) {
    File f = SPIFFS.open(path, "w");
    f.println(beatCounter);
    f.close();
    Debug.printf("Created cache entry: as part of set\n");
    cacheUpdate++;
  } else {
    Debug.printf("Removed cache entry: as part of set\n");
    SPIFFS.remove(path);
    cachePurge++;
  }
};

bool checkCache(const char * tag, unsigned long nowBeatCounter) {
  String path = uid2path(tag) + ".lastOK";
  bool present = SPIFFS.exists(path);

  if (!present) {
	cacheMiss++;
        Debug.printf("Returning cache miss\n");
	return false;
  };
  cacheHit++; 

  File f = SPIFFS.open(path, "r");
  unsigned long b = 0, age = 0;
  String l;

  if (!f) {
    Log.printf("Though cache file exists, it could not be opened.\n");
    goto ex;
  }

  l = f.readString();
  f.close();

  if (!l || l.length() < 3) {
    Log.printf("Though cache file exists, it seems bogus.\n");
    goto ex;
  }
  b = strtoul(l.c_str(), NULL, 10);
  if (b < 1581973038) {
    Log.printf("Though cache file exists, the value in it seems bogus.\n");
    goto ex;
  }

  age = nowBeatCounter - b;

  if (b && age < MAX_CACHE_AGE) {
        Debug.printf("Returning cache hit - age %u seconds\n", nowBeatCounter - b);
	return true;
  };
  Debug.printf("Cache miss - Too old %u >= %u\n",age, MAX_CACHE_AGE);

ex:
  SPIFFS.remove(path);
  Log.printf("Purging cache entry.\n");
  cachePurge++;
  return false;
};

void unsetCache(const char * tag) {
  String path = uid2path(tag) + ".lastOK";
  SPIFFS.remove(path);
}

#else
void prepareCache(bool wipe) { return; }
void setCache(const char * tag, bool ok, unsigned long beatCounter) { return; };
bool checkCache(const char * tag) { return false; };
void wipeCache() { return; };
void unsetCache(const char * tag) { return; };
#endif

