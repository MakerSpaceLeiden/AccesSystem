#ifndef _H_APPROVAL_BIN_FILE
#define _H_APPROVAL_BIN_FILE

#include "REST/ApprovalEntry.h"
#include "util/hex-util.h"

#ifndef TEST
#include <FS.h> 
#else
class File { 
public:
	File(const unsigned char * mem, size_t len) : _mem(mem), _len(len) { };
	~File() { };
	size_t read(unsigned char * buff, size_t len) {
		size_t l = len;
		if (l + at > _len)
			l = _len - at;
		memcpy(buff, _mem + at, l);
		at += l;
		return l;
	};
	bool seek(size_t pos) { 
		if (pos  >= _len) 
			return false; 
		at = pos; 
		return true; 
	};
	size_t available() { return _len - at; };
	size_t size() { return _len; };
private:
	const unsigned char * _mem = NULL;
	const size_t _len = 0;
	size_t at = 0;
};
#endif

// ApprovalBINFile is seperate from ApprovalAPI class as to allow
// us to compile it seperately without any Arduino/ESP32
// specific ness. This lets you make a binary utility
// in any unix/macosx for testing purposes.
//
class ApprovalBINFile {
public:
    ApprovalBINFile() {};
    ~ApprovalBINFile() {};
    
    ApprovalEntry * getEntry(File *f, const char * tag);
    bool import(File *f);
    
    inline unsigned long getIdentifier() { return identifier; };
    inline unsigned long getDataDate() { return datadate; };
    inline size_t getNumberOfTags() { return ntags; };

    typedef enum { UNK, MSLv1, MSLv2, MSLv3 } version_t;

    const char * versionStr() {
	return versionStr(version);
    };

    const char * versionStr(version_t v) { 
        switch(v) {
        case UNK: break;
        case MSLv1: return (const char *)"v1";
        case MSLv2: return (const char *)"v2";
        case MSLv3: return (const char *)"v3";
    };  return (const char *)"unk"; };

    void debug_dump();

private:

    // Medatadata on the data itself
    //
    unsigned long len_tag;      // length tag block
    unsigned long len_mem;      // length members block
   
    // copies of file content - for speed/ease 
    unsigned char salt[32];
    unsigned char keysalt[32];
    unsigned char ivs[32];

    // file offsets
    size_t ptr_tags; // we are not yet copying these- we could -  68 bytes/tag is teh cost
    size_t ptr_members;
    size_t ptr_eof;
    
    const size_t TAG_ENTRY_SIZE = 68;
    // Calculated - number of tags.
    size_t ntags;

    size_t getEntryPtr(File * f, unsigned char * saltedtag);

    version_t version;
    unsigned long identifier = 0;   // unqiue, opaque identifier
    unsigned long datadate = 0;     // date of creation
};
#endif

