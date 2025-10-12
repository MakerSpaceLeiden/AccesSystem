#ifndef _H_APPROVAL_ENTRY
#define _H_APPROVAL_ENTRY

#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <assert.h>

typedef unsigned char acl_t;
#define ACL_MASK_ACTIVE      (1)     // required to operate  / is set to active (see below APPROVE)
#define ACL_MASK_PERMIT      (2)     // requires instruction / has been given instruction
#define ACL_MASK_FORM        (4)     // requires the form to be filled out / has a form on file
#define ACL_MASK_APPROVE     (8)     // requires approval by the trustee / has been approved (active) or is defacto approved
#define ACL_MASK_INSTRUCTOR (16)     // can give instruction
#define ACL_MASK_BUDGET     (32)     // sufficient budget / needs to have budget
#define ACL_MASK_OVERRIDE   (64)     // requires override (machine is locked out)/has ability to
                                    // override a (locked) machine that needs this.

#ifdef TEST
// Avoid using anytihng that is Arduino/ESP32 specific.
//
#include <string>
#define String std::string
#else 
#include <Arduino.h>
#endif

class ApprovalEntry {
public:
    ApprovalEntry(String name, acl_t has, acl_t needs)
        : name(name), shortName(name), has(has),needs(needs) {};
    ApprovalEntry(String uid, String name, String shortName, acl_t has, acl_t needs)
        : name(name),  shortName(shortName), uid(uid), has(has),needs(needs) {};

    String name, shortName, uid;
    acl_t has, needs;
    bool ok() { return (has & needs) == needs; };

    const char * status() {
        if ((needs & ACL_MASK_ACTIVE) && (has & ACL_MASK_ACTIVE) == 0)
            return "tag inactive";
        if ((needs & ACL_MASK_PERMIT) && (has & ACL_MASK_PERMIT) == 0)
            return "no permit on file";
        if ((needs & ACL_MASK_FORM) && (has & ACL_MASK_FORM) == 0)
            return "no waiver on file";
        if ((needs & ACL_MASK_APPROVE) && (has & ACL_MASK_APPROVE) == 0)
            return "no trustee approval";
        if ((needs & ACL_MASK_OVERRIDE) && (has & ACL_MASK_OVERRIDE) == 0)
            return "machine out of order";
        if ((needs & ACL_MASK_BUDGET) && (has & ACL_MASK_BUDGET) == 0)
            return "no cash";

        if ((needs & has) == needs)
            return "has needed permissions";
        
        return "Machine disabled";
    };
};
#endif

