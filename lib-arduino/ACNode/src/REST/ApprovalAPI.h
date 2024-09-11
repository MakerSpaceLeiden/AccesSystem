#ifndef _REST_APPROVAL_API
#define _REST_APPROVAL_API

#include "REST/RestAPI.h"

typedef unsigned char acl_t;
#define ACL_MASK_ACTIVE     (1)
#define ACL_MASK_PERMIT     (2)
#define ACL_MASK_FORM       (4)
#define ACL_MASK_APPROVE    (8)

class ApprovalEntry {
public:
    ApprovalEntry(String name, acl_t has, acl_t needs) : name(name), has(has),needs(needs) {};
    String name;
    acl_t has, needs;
};

class ApprovalAPI {
public:
    ApprovalAPI() : blob(NULL) {};
    
    bool import(const unsigned char * binfile, size_t len);
    ApprovalEntry * getEntry(const char * tag);
    
private:
    //    RestAPI * _restAPI;
    
    const unsigned char * blob;
    unsigned long len_tag;
    unsigned long len_mem;
    
    const unsigned char * ptr_salt ;
    const unsigned char * ptr_keysalt;
    const unsigned char *  ptr_ivs;
    const unsigned char *  ptr_tags;
    const unsigned char *  ptr_members;
    const unsigned char *   ptr_eof;
    size_t ntags;
    
    const unsigned char * getEntryPtr(unsigned char * saltedtag);
};

#endif

