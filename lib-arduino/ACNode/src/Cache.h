#ifndef _CACHE_H
#define _CACHE_H

extern unsigned long cacheMiss, cacheHit;

void prepareCache(bool wipe);
void setCache(const char * tag, bool ok, unsigned long beatCounter);
bool checkCache(const char * tag);

#endif
