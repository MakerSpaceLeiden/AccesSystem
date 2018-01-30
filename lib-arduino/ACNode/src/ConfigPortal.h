#pragma once

#include "MakerSpaceMQTT.h"
extern char passwd[ 64 ];

extern void debugListFS(const char * path);
extern void configBegin();
extern int configLoad();
extern void configPortal();
extern void configRun();

