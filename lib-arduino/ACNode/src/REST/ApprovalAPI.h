#ifndef _REST_APPROVAL_API
#define _REST_APPROVAL_API

#include <ACBaseNode.h>
#include <ACBase.h>

#include "REST/RestAPI.h"
#include "REST/ApprovalEntry.h"
#include "REST/ApprovalBINFile.h"
#include "Display/Deck.h"

#ifndef ACL_URL
#define ACL_URL         "https://some-crm:4443/acl/api" // Instance of https://github.com/MakerSpaceLeiden/makerspaceleiden-crm
#endif

#define PATH_GETCOUNTER "/v1/getchangecounter"
#define PATH_GETTAGS    "/v2/gettags4machineBIN" // Version MSLv2 - with full name and uid reference.
#define PATH_RECORDUSE  "/v2/recorduse"

class ApprovalDeck;

// ApprovalBINFile is seperate from this class as to allow
// us to compile it seperately without any Arduino/ESP32
// specific ness. This lets you make a binary utility
// in any unix/macosx for testing purposes.
//
class ApprovalAPI : public ACBase, public ApprovalBINFile {
public:
    virtual const char *name() { return "ApprovalAPI"; };
    ApprovalAPI(RestAPI * r, const char * machine) : _restAPI(r), machine(machine) {};
    
    bool canApprove();
    
    void begin();
    void loop();

    void report(JsonObject & report);
    void status(JsonObject & report);

    void scheduleImmediateUpdate();
    void scheduleForcedReload();
    void scheduleCardused(const char * tag);
   
    void sendBestEffortTagApproved(const char * tag);
    inline unsigned long getLastUpdate() { return last_update; };

private:
    RestAPI * _restAPI = NULL;
    const char * machine;
    typedef enum update_res { NO_UPDATE_NEEDED, FAIL, NEEDS_UPDATE } update_t;
    
    unsigned long last_update = 0; // millis
    unsigned long interval = 0;
    update_t needsUpdate();
    void updateTagDB();

    // We'll have to revisit this for multi machine nodes; but sort of cannot
    // get round the fact that we need one per machine - or we need to modify
    // the file format to have a `which machine' flag.
    const char * TAGBINFILE = "/msl1.bin";
    void readCache();
    void writeCache();

    // friend class ApprovalDeck;
};

class ApprovalDeck : public Deck {
public:
    ApprovalDeck(ACNodeBase * node, ApprovalAPI * a) : Deck(node), _approvalAPI(a) {};
    virtual void render_pane(bool refresh);
private:
    ApprovalAPI * _approvalAPI;
};
#endif

