#ifndef _REST_APPROVAL_API
#define _REST_APPROVAL_API

#include <ACBaseNode.h>
#include <ACBase.h>

#include "REST/RestAPI.h"
#include "Display/Deck.h"

typedef unsigned char acl_t;
#define ACL_MASK_ACTIVE     (1)     // required to operate  / is set to active (see below APPROVE)
#define ACL_MASK_PERMIT     (2)     // requires instruction / has been given instruction
#define ACL_MASK_FORM       (4)     // requires the form to be filled out / has a form on file
#define ACL_MASK_APPROVE    (8)     // requires approval by the trustee / has been approved (active) or is defacto approved
#define ACL_MASK_OVERRIDE  (16)     // requires override/has ability to override a (locked) machine that needs this.
#ifndef ACL_URL
#define ACL_URL         "https://some-crm:4443/acl/api" // Instance of https://github.com/MakerSpaceLeiden/makerspaceleiden-crm
#endif

#define PATH_GETCOUNTER "/v1/getchangecounter"
#define PATH_GETTAGS    "/v1/gettags4machineBIN"

class ApprovalEntry {
public:
    ApprovalEntry(String name, acl_t has, acl_t needs) : name(name), has(has),needs(needs) {};
    String name;
    acl_t has, needs;
};

class ApprovalDeck;
class ApprovalAPI : public ACBase {
public:
    ApprovalAPI(RestAPI * r) : _restAPI(r), blob(NULL), identifier(0) {};
    
    ApprovalEntry * getEntry(const char * tag);
    
    void begin();
    void loop();
    void report(JsonObject& report);

    void scheduleImmediateUpdate();
    
private:
    RestAPI * _restAPI;
    
    const unsigned char * blob;
    size_t blob_len;

    // Medatadata on the data itself
    
    unsigned long len_tag;      // length tag block
    unsigned long len_mem;      // length members block
    
    // Offsets to varois entries.
    const unsigned char * ptr_salt ;
    const unsigned char * ptr_keysalt;
    const unsigned char * ptr_ivs;
    const unsigned char * ptr_tags;
    const unsigned char * ptr_members;
    const unsigned char * ptr_eof;
    
    // Calculated - number of tags.
    const size_t TAG_ENTRY_SIZE = 68;
    size_t ntags;

    unsigned long interval = 0;

    bool import(const unsigned char * binfile, size_t len);
    bool needsUpdate();
    void updateTagDB();
    
    const char * TAGBINFILE = "/msl1.bin";
    void readCache();
    void writeCache();

    const unsigned char * getEntryPtr(unsigned char * saltedtag);

friend class ApprovalDeck;
    unsigned long last_update = 0; // millis
    unsigned long identifier;   // unqiue, opaque identifier
    unsigned long datadate;     // date of creation
};

class ApprovalDeck : public Deck {
    void setApprovalAPI(ApprovalAPI * a) { _approvalAPI = a; };
private:
    ApprovalAPI * _approvalAPI;
    virtual void render_pane(bool refresh);
};
#endif

