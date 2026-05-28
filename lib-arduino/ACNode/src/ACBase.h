#ifndef _H_ACBASE
#define _H_ACBASE

#include <list>
#include <stddef.h>
#include <ArduinoJson.h>

#include <ExpandedGPIO.h>
#include "util/common-utils.h"

// #define ATX { const char * p = __FILE__; const char * q = rindex(p,'/'); Serial.printf("%s:%d %s\n", q ? q+1 : p, __LINE__, __PRETTY_FUNCTION__); Serial.flush(); delay(100); }
#define ATX { const char * p = __FILE__; const char * q = rindex(p,'/'); Serial.printf("%s:%d\n", q ? q+1 : p, __LINE__); Serial.flush(); }

typedef unsigned long beat_t;
extern beat_t beatCounter;      // My own timestamp - manually kept due to SPI timing issues.
extern beat_t beat_absdelta(beat_t a, beat_t b);

#define MAX_TOKEN_LEN  ( 128)
#define MAX_MSG        (2*1024)
#define MAX_HOST       (  48)
#define MAX_NAME       (  16)
#define MAX_TOPIC      ((MAX_NAME +1) * 3  + 1)

#define FILE2FIRMWARE(x) (rindex((x),'/') ? rindex((x),'/')+1 : (x))

// When defined - dump the average time spend in the loop() for each
// of the modules every 100 seconds.
//
#define PROFILE_BASE

class ACBase {
public:
    ACBase(const char * name = NULL) { if (name) _name = strdup(name); };
    ~ACBase() { if (_name) free(_name); }
    virtual const char * name() { return _name ? _name : "ACBase"; }

    typedef enum cmd_results { CMD_DECLINE, CMD_CLAIMED } cmd_result_t;
//    virtual cmd_result_t handle_cmd(ACRequest * req) { return CMD_DECLINE; };
    
    virtual void begin() { _isUp = true; return; };
    virtual void loop() { return; };
    virtual void stop() { return; };
    virtual void report(JsonObject report) { return; }
    virtual void status(JsonObject &status) { return; }
    
    virtual void set_debug(bool debug);

    virtual bool isUp() { return _isUp; }

    // Convenience shorthands
    int xdigitalRead(uint8_t pin) { return ExpandedGPIO::getInstance().xdigitalRead(pin); };
    void xdigitalWrite(uint8_t pin, uint8_t val) { ExpandedGPIO::getInstance().xdigitalWrite(pin, val); };
    void xanalogWrite(uint8_t pin, uint8_t val) { ExpandedGPIO::getInstance().xanalogWrite(pin, val); };
    void xpinMode(uint8_t pin, uint8_t mode) { ExpandedGPIO::getInstance().xpinMode(pin,mode); };
    unsigned int xanalogRead(uint8_t pin) { return ExpandedGPIO::getInstance().xanalogRead(pin); };

#ifdef PROFILE_BASE
    unsigned long micros_in_loop;
#endif
    
protected:
    bool _debug;
    bool _isUp;
    // protected:
   char * _name = NULL;
};

#if 0
class ACSecurityHandler : public ACBase {
public:
    virtual const char * name() { return "ACSecurityHandler"; }
    
    typedef enum acauth_results { DECLINE, FAIL, PASS, OK } acauth_result_t;
    
    virtual acauth_results helo(ACRequest * req) { return ACSecurityHandler::DECLINE; }
    virtual acauth_results verify(ACRequest * req) { return FAIL; }
    virtual acauth_results secure(ACRequest * req) { return FAIL; }
    virtual acauth_results cloak(ACRequest * req) { return FAIL; }
};
#endif
#endif
