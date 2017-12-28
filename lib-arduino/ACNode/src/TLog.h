#include <MakerSpaceMQTT.h>
#include <Print.h>

#ifndef _H_TLOG
#define _H_TLOG

// 'tee' class - which will log both to serial and to
// MQTT if the latter is alive.
//
class TLog : public Print {
  public:
    //TLog(bool hasSyslog);
    TLog();
    void begin(const char * prefix = "Log", int speed=9600, uint16_t syslogPort = 514);
    virtual size_t write(uint8_t c);
  private:
    char logtopic[MAX_TOPIC], logbuff[MAX_MSG];
    size_t at;
    uint16_t _syslogPort;
    bool _doSerial, _doMqtt;
  protected:
    //bool _hasSyslog;
};

extern TLog Log;
// Wether or not to log at Debug message level. Doing so
// may well leak sensitive details, like passwords.
//
#ifdef DEBUG
#define Debug Serial
#else
#define Debug if (0) Log
#endif
#endif
extern void debugFlash();
