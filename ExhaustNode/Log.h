#pragma once

#include "MakerspaceMQTT.h"

// 'tee' class - which will log both to serial and to
// MQTT if the latter is alive.
//
class Log : public Print {
  public:
    void begin(const char * prefix, int speed);
    virtual size_t write(uint8_t c);
  private:
    char logtopic[MAX_TOPIC], logbuff[MAX_MSG];
    size_t at;
};

// Wether or not to log at Debug message level. Doing so
// may well leak sensitive details, like passwords.
//
#ifdef DEBUG
#define Debug Serial
extern void debugFlash();
#else
#define Debug if (0) Log
#endif

