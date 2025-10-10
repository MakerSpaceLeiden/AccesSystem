#ifndef _H_APPROVAL_BIN_FILE
#define _H_APPROVAL_BIN_FILE

#include "REST/ApprovalEntry.h"

// ApprovalBINFile is seperate from ApprovalAPI class as to allow
// us to compile it seperately without any Arduino/ESP32
// specific ness. This lets you make a binary utility
// in any unix/macosx for testing purposes.
//

class ApprovalBINFile {
public:
    ApprovalBINFile() {};
    ~ApprovalBINFile();
    
    ApprovalEntry * getEntry(const char * tag);
    bool import(const unsigned char * binfile, size_t len);
    
    inline unsigned long getIdentifier() { return identifier; };
    inline unsigned long getDataDate() { return datadate; };
    inline size_t getNumberOfTags() { return ntags; };

    const char * versionStr() { switch(version) {
        case UNK: break;
        case MSLv1: return (const char *)"v1";
        case MSLv2: return (const char *)"v2";
    };  return (const char *)"unk"; };

protected:
    const unsigned char * blob = NULL;
    size_t blob_len = 0;
private:

    // Medatadata on the data itself
    //
    unsigned long len_tag;      // length tag block
    unsigned long len_mem;      // length members block
    
    // Offsets to various entries.
    //
    const unsigned char * ptr_salt ;
    const unsigned char * ptr_keysalt;
    const unsigned char * ptr_ivs;
    const unsigned char * ptr_tags;
    const unsigned char * ptr_members;
    const unsigned char * ptr_eof;
    
    const size_t TAG_ENTRY_SIZE = 68;
    // Calculated - number of tags.
    size_t ntags;

    const unsigned char * getEntryPtr(unsigned char * saltedtag);

    enum { UNK, MSLv1, MSLv2 } version;
    
    unsigned long identifier = 0;   // unqiue, opaque identifier
    unsigned long datadate = 0;     // date of creation
};
#endif

