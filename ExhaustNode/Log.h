#include "MakerspaceMQTT.h"
#pragma once

class Log : public Print {
  public:
    void begin(const char * prefix, int speed);
    virtual size_t write(uint8_t c);
  private:
    char logtopic[MAX_TOPIC], logbuff[MAX_MSG];
    size_t at;
};

#ifdef DEBUG
#define Debug Serial
#else
#define Debug if (0) Log
#endif

