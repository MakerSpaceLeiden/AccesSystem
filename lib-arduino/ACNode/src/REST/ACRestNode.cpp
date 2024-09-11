#include "Rest/ACRestNode.h"
#include <ArduinoJSON.h>

ACNodeRest::ACNodeRest(const char * machine, const char * ssid, const char * ssid_passwd) : super(machine,ssid,ssid_passwd) {
    CONSTS();
    pop();
};

ACNodeRest::ACNodeRest(const char * machine, bool wired) : super(machine, wired) {
    CONSTS();
    pop();
};

void ACNodeRest::CONSTS() {
    super::CONSTS();
}

void ACNodeRest::pop() {
    super::pop();
    
    addHandler(&_restAPI);
    
}

void ACNodeRest::begin(eth_board_t board, uint8_t clear_button) {
    super::begin(board,clear_button);
    
    // Dirty hack to stripe unique postfix of the name when testing.
    //
    char * p = strdup(moi);
    if (0 == strncmp(p,"test-",5)) {
        char * q = rindex(p,'-');
        if (q) *q = 0;
        Log.printf("Simplifying name %s to %s\n", moi, p);
    };
    _restAPI.setTerminalname(p);
}

void request_approval(const char * tag, const char * operation = NULL, const char * target = NULL, bool useCacheOk = true) {
    
};

void ACNodeRest::loop() {
    super::loop();
    
    if (_restAPI.state() != RestAPI::FULLY_REGISTERED)
        return;
    
    static unsigned long lst = 0;
    if (!(lst == 0 || millis() - lst > 3600 * 1000))
        return;
    lst = millis();
    
#define ACL_URL         "https://makerspaceleiden.nl:4443/crm/acl/api"
#define PATH_GETCOUNTER "/v1/getchangecounter"
#define PATH_GETTAGS    "/v1/gettags4machineBIN"
    
    Log.println("** Update counter");
    JsonDocument res = _restAPI.rest(ACL_URL PATH_GETCOUNTER);
    Log.print("result: "); serializeJsonPretty(res, Log);Log.println();
    
    const char * line = res.as<String>().c_str();
    int cntr = atoi(line);
    Log.printf("   %d\n", cntr);
    
    Log.println("** Update tag DB");
    char url[256];
    snprintf(url,sizeof(url), ACL_URL PATH_GETTAGS "/%s", "Pottery%20oven" /* machine */);
    res = _restAPI.rest(url);
    
    ApprovalAPI ap = ApprovalAPI();
    ap.import(res);
    
}

