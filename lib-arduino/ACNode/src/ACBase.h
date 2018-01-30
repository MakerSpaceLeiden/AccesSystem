#ifndef _H_ACBASE
#define _H_ACBASE

#include <list>
#include <stddef.h>

#include "MakerSpaceMQTT.h"

typedef unsigned long beat_t;
extern beat_t beatCounter;      // My own timestamp - manually kept due to SPI timing issues.

class ACRequest {
    public:
	ACRequest();
        ACRequest(const char * _topic, const char * _payload) {
		strncpy(topic, _topic, sizeof(topic));
		strncpy(payload, _payload, sizeof(payload));
		strncpy(rest, _payload, sizeof(payload));
	};
        beat_t beatReceived;
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
    const char * name = NULL;

    typedef enum cmd_results { CMD_DECLINE, CMD_CLAIMED } cmd_result_t;

    // virtual void begin(ACNode &acnode);

    void begin() { return; };
    void loop() { return; };
    
    cmd_result_t handle_cmd(ACRequest * req) { return CMD_DECLINE; };
    
    void set_debug(bool debug);
  protected:
    bool _debug;
  // protected:
};


class ACSecurityHandler : public ACBase {
  public:
    const char * name = NULL;
    
   typedef enum acauth_results { DECLINE, FAIL, PASS, OK } acauth_result_t;

   acauth_result_t verify(ACRequest * req) { return FAIL; }

   acauth_results secure(ACRequest * req) { return FAIL; }

   acauth_results cloak(ACRequest * req) { return FAIL; }
};

#endif
