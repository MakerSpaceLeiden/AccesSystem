#ifndef _CACHE_H
#define _CACHE_H

// up to 48 hours of caching.
//
#define MAX_CACHE_AGE	(2 * 24 * 3600 * 1000)
extern unsigned long cacheMiss, cacheHit;

void prepareCache(bool wipe);
void setCache(const char * tag, bool ok, unsigned long beatCounter);
bool checkCache(const char * tag, unsigned long nowBeatCounter);
void wipeCache();
#endif
