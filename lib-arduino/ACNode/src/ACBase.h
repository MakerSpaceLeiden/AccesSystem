#ifndef _H_ACBASE
#define _H_ACBASE

#include <list>
#include <stddef.h>

#include "MakerSpaceMQTT.h"

typedef unsigned long beat_t;
extern beat_t beatCounter;      // My own timestamp - manually kept due to SPI timing issues.
extern beat_t beat_absdelta(beat_t a, beat_t b);

class ACRequest {
public:
    ACRequest() { topic[0] = payload[0] = rest[0] = 0; };
    ACRequest(const char * _topic, const char * _payload) {
        strncpy(topic, _topic, sizeof(topic));
        strncpy(payload, _payload, sizeof(payload));
        strncpy(rest, _payload, sizeof(payload));
    };
    // raw data as/when received:
    char topic[MAX_MSG];
    char payload[MAX_MSG];

    // data as extracted from any payload.
    beat_t beatExtracted;
    char version[32];
    char beat[MAX_MSG];
    char cmd[MAX_MSG];
    char rest[MAX_MSG];
    char tag[MAX_MSG];
};

class ACBase {
public:
    virtual const char * name() { return "ACBase"; }
    
    typedef enum cmd_results { CMD_DECLINE, CMD_CLAIMED } cmd_result_t;
    
    virtual void begin() { return; };
    virtual void loop() { return; };
    virtual void stop() { return; };
    
    virtual cmd_result_t handle_cmd(ACRequest * req) { return CMD_DECLINE; };
    
    virtual void set_debug(bool debug);
protected:
    bool _debug;
    // protected:
};


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
