#ifndef _H_ACNODE_SIG
#define _H_ACNODE_SIG

#include "ACBaseNode.h"
#include "ACNode-private.h"
#include "SIG/ACNode.h"

// #define HAS_MSL
// #define HAS_SIG1
#define HAS_SIG2

class ACNode : public ACNodeBase {
public:
    ACNode(const char * machine, const char * ssid, const char * ssid_passwd, acnode_proto_t proto = PROTO_SIG2);
    ACNode(const char * machine = NULL, bool wired = true, acnode_proto_t proto = PROTO_SIG2);

#ifdef INPUT
    void begin(eth_board_t board = BOARD_AART, uint8_t clear_button = INPUT /* Olimex BUT1 */);
#else
    void begin(eth_board_t board = BOARD_AART, uint8_t clear_button = -1);
#endif

#ifdef HAS_SIG2
    void add_trusted_node(const char *node);
#endif

    void addSecurityHandler(ACSecurityHandler *handler);
   
    char * cloak(char *tag);
    void send_helo(char * tokenOrNull = NULL);

    unsigned long uptimeInSeconds() { return _start_beat ?  beatCounter - _start_beat : 0; };

    // Public - so it can be called from our fake
    // singleton. Once that it solved it should really
    // become private again.
    //
    void send(const char * payload) { send(NULL, payload, false); };
    void send(const char * topic, const char * payload, bool raw = false);

    void request_approval(const char * tag, const char * operation = NULL, const char * target = NULL, bool useCacheOk= true);

    // This function should be private - but we're calling
    // it from a C callback in the mqtt subsystem.
    //
    void process(const char * topic, const char * payload);
    void report(JsonObject & report);

private:
    std::list<ACSecurityHandler*> _security_handlers;
    MqttStream  * mqttlogStream;
    cmd_result_t handle_cmd(ACRequest * req);

protected:
    acnode_proto_t _proto;
    void pop();

    void configureMQTT();
    void reconnectMQTT();
    void mqttLoop();

};

// For use in callbacks that are from plain C
extern ACNode *_acnode;

extern void send(const char * topic, const char * payload);

#include "SIG/Beat.h"

#include "SIG/MSL.h"
#include "SIG/SIG1.h"
#include "SIG/SIG2.h"

#include "OTA.h"

#endif
