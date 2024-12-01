#include "REST/ApprovalAPI.h"

#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <assert.h>
#include <FS.h>
#include <SPIFFS.h>

#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>

/* Compact/IoT oriented version of the tag/acl data; with the names
 * encrypted against a key derived from, in part, the tag that was
 * swiped. It consists of an index with one (salted hashed) entry
 * for each tag and an offset pointing to a user record. The user
 * record has the name and the permissions. The reason for this
 * split is that a user can have multiple tags. However the cost
 * for this is 32+4=36 bytes; while a fair number of the actual
 * member records are smaller than that. So we may need to revisit
 * this at some point in the future.
 *
 *  idx len
 *   0     4                      MSL1 -- version of this file
 *   4     4                      Unique Identifier; 4 byte octed sequence
 *   8     4                      timestamp, unix epoch, in seconds, network (big endian) order
 *   12    4                      LT Length of the tag section, network order
 *   16    4                      LM Length of the members section, network order
 *   20   32                      salt for the tags
 *   52   32                      salt for the keys
 *   84   32                      IV seed for the iv used in encrypting the member names
 *  116   LT                      tag section; N entries of XX bytes = LT long
 *        68      0   32          salted tag; sha256( salt || tag-as-ascii)
 *                32  32          decryption key user name; xor(salted tag, sha255( tag || key salt)
 *                64  04          Index into the member section, 4 bytes, network order
 *  116+LT        LM              member section
 *       *        0    1          has
 *                1    1          needs
 *                2    1          LES: Length encrypted section
 *                3    LES        AES-CBC, padded, ecrypted with the first 16 bytes of
 *                                sha256( iv || Index) as the iv and key from tag section.
 *
 *  116+LT+LM                      EOF
 *
 # Tags are in ascii (not binary) format; with no leading zeros; i.e. \d{1,3}[-\d{1,3}]*
 # Example: 1-2-3-210-10
 # The decoded name is UTF-8.
 */

static void prepareCache(bool wipe) {
    Log.println(wipe ? "Resetting Filesystem" : "Mounting Filesystem");
    if (wipe)
        SPIFFS.format();
    
    if (!SPIFFS.begin()) {
        Log.println("Mount failed - trying to reformat");
        if (!SPIFFS.format() || !SPIFFS.begin()) {
            Log.println("SPIFFS mount after re-formatting also failed. Giving up. No caching.");
            return;
        };
    };
    Log.println("Filesystem ready.");
};

void ApprovalAPI::readCache() {
    File f = SPIFFS.open(TAGBINFILE, "r");
    if (!f) {
        Log.println("No cache yet");
        return;
    };
    size_t len = f.size();
    const unsigned char * tmp = (const unsigned char *)malloc(len);
    if (!tmp) {
        Log.println("Cache malloc failed.");
        return;
    };
    if (len == f.read((uint8_t*)tmp,len))
        import(tmp,len);
    else {
        Log.println("Cache loading failed.");
        free((void*)tmp);
    };
    
    f.close();
}

void ApprovalAPI::writeCache() {
    File f = SPIFFS.open(TAGBINFILE, "w");
    if (!f) {
        Log.println("Failed to open cache for writing");
        return;
    }
    size_t l = f.write((uint8_t*)blob,blob_len);
    f.close();
    
    if (l != blob_len) {
        Log.println("Cache writing failed. Deleting corrupted file.");
        SPIFFS.remove(TAGBINFILE);
    }
}

void ApprovalAPI::begin() {
    prepareCache(false);
    readCache();
};

void ApprovalAPI::scheduleImmediateUpdate() {
    if (millis() - last_update > 5 * 1000)
        interval = 0;
    else
        interval = 5000;
}

void ApprovalAPI::scheduleForcedReload() {
    interval = 999;
}

void ApprovalAPI::loop() {
    // We need authentication before we can do anything.
    //
    if (_restAPI->state() != RestAPI::FULLY_REGISTERED && _restAPI->state() != RestAPI::DONE)
        return;
    
    if (interval && last_update &&  millis() - last_update < interval)
        return;
    
    
    update_t t;
    if (interval == 999) {
        Log.println("Executing forced update");
        t = NEEDS_UPDATE;
    }
    else
        t = needsUpdate();
    
    switch(t) {
        case FAIL:
            Debug.println("TagDB update failed, scheduling retry");
            interval =  (600  + (esp_random() & 0xFF)) * 1000;
            break;
        case NO_UPDATE_NEEDED:
            Debug.println("No TagDB update needed");
            interval =  (3600  + (esp_random() & 0xFF)) * 1000;
            break;
        case NEEDS_UPDATE:
            updateTagDB();
            // Retry relatively soon - as updates tend to come in blocks. And
            // if above failed - we want to retry quite soon too as well.
            //
            interval =  (200  + (esp_random() & 0xFF)) * 1000;
            break;
    };
    last_update = millis();
};

ApprovalAPI::update_t ApprovalAPI::needsUpdate() {
    unsigned char * buff = NULL;
    char url[] = ACL_URL PATH_GETCOUNTER;
    size_t len = 1024;
    int n = _restAPI->get(url,&len,&buff);
    if (n < 0)
        return FAIL;
    buff[n] = 0; // damages last byte.
    
    unsigned long cntr = atoi((char *)buff);
    Log.printf("TagDB identifier: %08x: %s (previous: %08x)\n", cntr, (identifier == cntr) ? "no changes" : "*Changed!*", identifier);
    
    last_update = millis();
    
    return (cntr != identifier) ? NEEDS_UPDATE : NO_UPDATE_NEEDED;
}

void ApprovalAPI::updateTagDB() {
    char url[256];
    char tmp[64];
    snprintf(url,sizeof(url), ACL_URL PATH_GETTAGS "/%s", _argencode(tmp,sizeof(tmp),machine));
    
    unsigned char * buff = NULL;
    size_t len = 32 * 1024; // ~1k/10 active users -- so enough for 300+ users.
    int n = _restAPI->get(url,&len,&buff);
    if (n <= 0) {
        Log.printf("Failed to load bintags from <%s>\n", url);
        return;
    };
    if (import(buff,len)) {
        writeCache();
    } else {
        Log.println("Failed to load.");
        free((void*)buff);
    };
    return;
}

bool ApprovalAPI::import(const unsigned char * binfile, size_t len) {
    
    const unsigned char prefix1[] = { 0x4d, 0x53, 0x4c, 0x31 }; // MSL1
    const unsigned char prefix2[] = { 0x4d, 0x53, 0x4c, 0x32 }; // MSL2
    
    version =  UNK;
    if (!bcmp(prefix1, binfile, 4))
        version = MSLv1;
    else if (!bcmp(prefix2, binfile, 4))
        version = MSLv2;
    else {
        Log.printf("Unknown tagblob version\n");
        return false;
    };
    
    identifier = ntohl( *(uint32_t*)(binfile + 4) );
    datadate = ntohl( *(uint32_t*)(binfile + 8) );
    len_tag = ntohl( *(uint32_t*)(binfile + 12) );
    len_mem = ntohl( *(uint32_t*)(binfile + 16) );
    ptr_salt = binfile + 20;
    ptr_keysalt = ptr_salt + 32;
    ptr_ivs = ptr_keysalt + 32;
    ptr_tags = ptr_ivs + 32;
    
    ptr_members = ptr_tags + len_tag;
    ptr_eof = ptr_members + len_mem;
    
    ntags = len_tag / TAG_ENTRY_SIZE;
    
    if (ptr_eof != binfile + len || len_tag - ntags * TAG_ENTRY_SIZE != 0) {
        Log.printf("Tagblob currupted\n");
        return false;
    };
    Log.printf("Loaded %lu TAGs with ID 0x%08x, size %lu, version %s, dated %s",
               ntags, identifier, len,
               version == MSLv2 ? "MSLv2" : "MSLv1",
               ctime((const time_t *) &datadate)
               );
    
    // Swap the file in; take over the malloc/free
    //
    if (blob) free((void*)blob);
    blob = binfile;
    blob_len = len;
    return true;
}

void ApprovalAPI::report(JsonObject& report) {
    report["bintag_id"] = identifier;
    report["bintag_ntags"] = ntags;
    char buff[32] = "never";
    
    
    if (datadate) {
        strncpy(buff, ctime((const time_t *) &datadate),32);
        buff[25]='\0';
    };
    report["bintag_date"] = buff;
};

/* Simple binary search for a 32 byte hasn strh.
 */
const unsigned char * ApprovalAPI::getEntryPtr(unsigned char * saltedtag) {
    for(int low = 0, high = ntags-1; low <= high;) {
        int mid = low + (high - low) / 2;
        const unsigned char * ptr = ptr_tags + mid * TAG_ENTRY_SIZE;
        int c = bcmp(ptr, saltedtag, 32);
        if (c == 0)
            return ptr;
        else if (c < 0)
            low = mid + 1;
        else
            high = mid - 1;
    }
    return NULL;
}

bool ApprovalAPI::canApprove() {
    return blob ? true : false;
}

ApprovalEntry * ApprovalAPI::getEntry(const char * tag) {
    if (!blob) {
        Log.println("getEntry: No data (yet)");
        return NULL;
    };
    
    unsigned char saltedtag[32];
    
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    
    mbedtls_sha256_starts_ret(&sha_ctx, 0);
    mbedtls_sha256_update_ret(&sha_ctx, ptr_salt, 32);
    mbedtls_sha256_update_ret(&sha_ctx, (unsigned char*) tag, strlen(tag));
    mbedtls_sha256_finish_ret(&sha_ctx, saltedtag);
    mbedtls_sha256_free(&sha_ctx);
    
    const unsigned char * ptr = getEntryPtr(saltedtag);
    if (!ptr)
        return NULL;
    
    const unsigned char * tagkey = ptr + 32;
    unsigned int idx =  ntohl( *(uint32_t*)(ptr + 64));
    
    if (idx > len_mem - 3 - 16) {
        printf("Corr 0\n");
        return NULL;
    };
    
    unsigned char * mptr = (unsigned char*) ptr_members + idx;
    if (mptr > ptr_eof) {
        Log.println("getEntry: Corrupted entry");
        return NULL;
    };
    
    acl_t has = (acl_t)*(unsigned char*)(mptr + 0);
    acl_t needs = (acl_t)*(unsigned char*)(mptr + 1);
    size_t paddedlen = (size_t)*(unsigned char*)(mptr + 2);
    unsigned char * padded_enc_name = (unsigned char *)mptr + 3;
    
    if (paddedlen % 16) {
        Log.println("getEntry: size not a multiple of 16");
        return NULL;
    };
    if (paddedlen > 256 || paddedlen < 2) {
        Log.printf("getEntry: %u too large/small\n",paddedlen);
        return NULL;
    };
    // Construct the decryption key for the user entry; from
    // the key entry
    //
    unsigned char saltkey[32];
    mbedtls_sha256_starts_ret(&sha_ctx, 0);
    mbedtls_sha256_update_ret(&sha_ctx, (unsigned char*) tag, strlen(tag));
    mbedtls_sha256_update_ret(&sha_ctx, ptr_keysalt, 32);
    mbedtls_sha256_finish_ret(&sha_ctx, saltkey);
    
    unsigned char dec[32];
    memcpy((void*)dec,(void*)tagkey,32);
    for(int i = 0; i < 32; i++)
        dec[i] ^= saltkey[i];
    
    // Construct the IV for this user entry from the iv salt
    // and the index. Note that we're only using the first 16
    // bytes as the actual IV.
    //
    unsigned char uiv[32];
    mbedtls_sha256_starts_ret(&sha_ctx, 0);
    mbedtls_sha256_update_ret(&sha_ctx, ptr_ivs, 32);
    mbedtls_sha256_update_ret(&sha_ctx, ptr + 64, 4); // In network order.
    mbedtls_sha256_finish_ret(&sha_ctx, uiv);
    
    unsigned char plaintext[paddedlen]; // i.e. include any padding.
    
    mbedtls_aes_context aes;
    memset(&aes, 0, sizeof(mbedtls_aes_context));
    mbedtls_aes_init(&aes);
    if (
        (0 != mbedtls_aes_setkey_dec(&aes, dec, 256)) ||
        (0 != mbedtls_aes_crypt_cbc( &aes, MBEDTLS_AES_DECRYPT, paddedlen, uiv, padded_enc_name, plaintext ))
        ) {
            Log.println("getEntry: Failed to CBC decrypt");
            return NULL;
        };
    
    mbedtls_aes_free(&aes);
    
    // Removed PKCS#7 padding - as traditionally used with AES.
    // https://www.ietf.org/rfc/rfc2315.txt; section 10.3, page 21 Note 2.
    //
    uint8_t pad = plaintext[paddedlen-1];
    if (pad >= paddedlen) {
        Log.printf("getEntry: Failed to decrypt - padding problem (%u>=%u)\n", pad, paddedlen);
        return NULL;
    };
    
    // for(;pad;pad--)
        plaintext[paddedlen - pad] = '\0';
    
    if (version == MSLv1)
        return new ApprovalEntry((char*)plaintext, has, needs);
    
    // For version MSLv2 we have 3; \0 terminated fields.
    //
    char * p = (char*) plaintext;
    char * uid = p; p += strlen(uid) +1;
    
    if (p > (char*)plaintext + sizeof(plaintext)) {
        Log.println("getEntry: malformed uid");
        return NULL;
    };
    
    char * shortName = p; p += strlen(shortName) +1;
    if (p > (char*)plaintext + sizeof(plaintext)) {
        Log.println("getEntry: malformed shortname");
        return NULL;
    };
    char * name = p;
    if (p+strlen(name) > (char*)plaintext + sizeof(plaintext)) {
        Log.println("getEntry: malformed name");
        return NULL;
    };

    return new ApprovalEntry(uid, name, shortName, has, needs);
}

void ApprovalDeck::render_pane(bool refresh) {
    if(!refresh)
        return;
    
    _display->print_centred("TAG DB");
    if (!_approvalAPI) {
        _display->printf("not ready");
        return;
    };
    _display->printf("ID   :%08x\n",_approvalAPI->identifier);
    
    struct tm * t = gmtime((const time_t *)&(_approvalAPI->datadate));
    char ds[10], ts[10];
    strftime(ds,sizeof(ds),"%Y-%m-%d",t);
    strftime(ts,sizeof(ts),"%H:%M:%S",t);
    _display->printf("Dated:%s\n",ds);
    _display->printf("      %sZ\n",ts);
    _display->printf("Age  :%s\n\n",since(_approvalAPI->datadate));
    _display->printf("Check:%s ago\n", _approvalAPI->last_update ?
                     since((millis() - _approvalAPI->last_update)/1000) : "never");
};


#ifdef TEST
#include <iostream>
#include <list>
#include <string>

const unsigned char testfile[1021] = {
    /*
     Format:    MSL1
     Header     116
     Tags:      748
     Users:     165
     
     Salt=13b77375a4fa7b913fa471b4cf85ad53d32037e8e6f349793e1b472171131856
     saltedtag=8d0ae8e1ec9be3431d24c5e43c24ab74e3f8e510808c8af8155f9d20f700586f
     
     Tag: 1-2-5 -- Leo Tags
     saltedkey=103d456e2ac4ec5f9962c75bf485df85d0d11909e5f051410a7101603a9aa316
     tagkey=0c0a76e0b23c5e7900b587040aa0437a143846cbec90a4e504dca2d05fef180a
     index=89
     
     User entry -- index=116 + 748 + 89
     H,N,len=1,5,16
     uiv=31d1f3b18782b72aff3f25cc82744743
     enc=d1a50e0cb0641933c0bc3f1f6172ca23
     key=1c37338e98f8b22699d7405ffe259cffc4e95fc20960f5a40eada3b06575bb1c
     resulting clr=4c656f20546167730808080808080808 (sans padding)
     
     */
    0x4d, 0x53, 0x4c, 0x31, 0x00, 0x00, 0x00, 0xce, 0x66, 0xe1, 0xc6, 0xd9, 0x00, 0x00, 0x02, 0xec,
    0x00, 0x00, 0x00, 0xa5, 0x13, 0xb7, 0x73, 0x75, 0xa4, 0xfa, 0x7b, 0x91, 0x3f, 0xa4, 0x71, 0xb4,
    0xcf, 0x85, 0xad, 0x53, 0xd3, 0x20, 0x37, 0xe8, 0xe6, 0xf3, 0x49, 0x79, 0x3e, 0x1b, 0x47, 0x21,
    0x71, 0x13, 0x18, 0x56, 0xd8, 0x35, 0x8a, 0xf7, 0xd9, 0x9e, 0x8c, 0x49, 0xbc, 0x7c, 0x16, 0xfe,
    0x5c, 0xfd, 0x69, 0x05, 0x42, 0xd9, 0x88, 0x49, 0x57, 0x50, 0xf0, 0x4b, 0x40, 0x20, 0x65, 0x2c,
    0x9f, 0xc7, 0x98, 0xb4, 0x6a, 0xac, 0xed, 0x73, 0x6e, 0x3b, 0x5c, 0x51, 0xf2, 0x7d, 0x29, 0xbc,
    0x42, 0x18, 0xac, 0x5f, 0xf3, 0x93, 0x15, 0xb9, 0xde, 0x02, 0xab, 0xca, 0xaa, 0x08, 0xca, 0x6d,
    0x4f, 0xc0, 0x8a, 0x05, 0x00, 0x46, 0x11, 0xdf, 0x15, 0x65, 0x92, 0xac, 0x09, 0x90, 0xb4, 0x48,
    0x0a, 0xbf, 0x3c, 0x06, 0x82, 0xbb, 0x34, 0x71, 0xa1, 0x00, 0x64, 0x46, 0xb8, 0xa3, 0xc0, 0xad,
    0x7d, 0xf6, 0xc9, 0x8c, 0xe5, 0x39, 0xa8, 0xd0, 0x86, 0x1a, 0x8c, 0x20, 0xc1, 0x9c, 0x37, 0x44,
    0xc7, 0x0c, 0x75, 0xe9, 0xd1, 0x9a, 0x26, 0x86, 0xe4, 0xb7, 0x71, 0xac, 0x00, 0x44, 0x4f, 0x47,
    0x84, 0xfd, 0xc5, 0x38, 0x00, 0x00, 0x00, 0x7f, 0x10, 0x06, 0x39, 0x3c, 0x13, 0x97, 0xd7, 0x67,
    0x28, 0x8a, 0x63, 0xd5, 0xd7, 0xb0, 0x0e, 0x21, 0xd3, 0x2c, 0xf0, 0x86, 0x81, 0x28, 0x80, 0xb5,
    0xbb, 0x59, 0xbe, 0x9a, 0x68, 0xaa, 0xac, 0x1f, 0xad, 0x68, 0x16, 0x83, 0x43, 0x48, 0x64, 0x45,
    0x84, 0xec, 0x47, 0xf3, 0x85, 0xe5, 0xda, 0x37, 0x33, 0x33, 0x62, 0xa8, 0x22, 0xff, 0x76, 0xf1,
    0x39, 0x29, 0x73, 0xf1, 0x1c, 0x60, 0xef, 0xfa, 0x00, 0x00, 0x00, 0x59, 0x41, 0x85, 0xee, 0xcc,
    0x58, 0x85, 0x41, 0x82, 0x38, 0x55, 0xcc, 0x35, 0x90, 0xee, 0x4a, 0x29, 0x80, 0x61, 0x2a, 0xcd,
    0xe0, 0x88, 0x4f, 0xf6, 0x0d, 0x67, 0x86, 0x6b, 0xdf, 0x56, 0xac, 0x8b, 0x8c, 0xe1, 0xff, 0xe9,
    0xc4, 0x2a, 0xe4, 0xe3, 0x05, 0x78, 0xcb, 0xf8, 0xb8, 0x03, 0x82, 0x87, 0xc4, 0x46, 0xf7, 0x59,
    0x3e, 0x17, 0xa3, 0x2d, 0x9d, 0x11, 0xde, 0x7d, 0xdc, 0x06, 0xad, 0xf2, 0x00, 0x00, 0x00, 0x59,
    0x64, 0x9e, 0x73, 0xfe, 0xb3, 0xab, 0xdf, 0x9c, 0xc1, 0xa4, 0x5a, 0x7a, 0x2c, 0x30, 0x77, 0xc4,
    0x6d, 0x4a, 0xe4, 0x4d, 0x25, 0x33, 0x4b, 0x8b, 0x98, 0x2e, 0x9d, 0x1d, 0x69, 0xa3, 0x92, 0xd6,
    0x5d, 0x45, 0x9d, 0xb0, 0x7e, 0x2a, 0xf9, 0x0e, 0x2f, 0xa0, 0xc3, 0x2d, 0x16, 0x6a, 0xa3, 0x53,
    0x6c, 0x4d, 0x43, 0xe2, 0xfd, 0x4a, 0x87, 0x43, 0x59, 0x11, 0x4b, 0x80, 0xdd, 0xf3, 0x7b, 0x47,
    0x00, 0x00, 0x00, 0x59, 0x6c, 0x7f, 0xde, 0x29, 0x6e, 0x82, 0x56, 0x9c, 0xe7, 0x30, 0xb5, 0x90,
    0x72, 0x80, 0xed, 0x87, 0x34, 0x4d, 0xb1, 0x1c, 0xfa, 0xc4, 0x9d, 0xdb, 0x54, 0xdc, 0x2c, 0x13,
    0xc3, 0x8b, 0xd6, 0xf6, 0x99, 0x91, 0xb6, 0x1f, 0x8a, 0x20, 0xb9, 0xc6, 0xd8, 0x98, 0x88, 0x20,
    0xe1, 0x8a, 0x65, 0x5e, 0xf3, 0xe6, 0xc2, 0x53, 0x16, 0x07, 0x87, 0x79, 0xfb, 0x0b, 0xca, 0x64,
    0xf4, 0x7d, 0x4d, 0xa7, 0x00, 0x00, 0x00, 0x59, 0x79, 0x4a, 0xee, 0xf7, 0x4d, 0x6d, 0xcd, 0x9f,
    0x5e, 0xdd, 0x88, 0x74, 0x86, 0x6e, 0x21, 0xd8, 0x76, 0x5e, 0xe9, 0xea, 0x53, 0xb6, 0x9b, 0x48,
    0xc3, 0xc8, 0x66, 0x09, 0x40, 0x51, 0xe6, 0x4e, 0x30, 0x81, 0x26, 0x90, 0xaf, 0x44, 0x11, 0x9e,
    0x0c, 0x15, 0x50, 0xff, 0x5c, 0x28, 0xea, 0x62, 0x44, 0x1b, 0xe0, 0x17, 0x10, 0xec, 0x18, 0xd1,
    0x26, 0x9c, 0x7e, 0xa6, 0x9d, 0x22, 0x6b, 0x50, 0x00, 0x00, 0x00, 0x59, 0x8d, 0x0a, 0xe8, 0xe1,
    0xec, 0x9b, 0xe3, 0x43, 0x1d, 0x24, 0xc5, 0xe4, 0x3c, 0x24, 0xab, 0x74, 0xe3, 0xf8, 0xe5, 0x10,
    0x80, 0x8c, 0x8a, 0xf8, 0x15, 0x5f, 0x9d, 0x20, 0xf7, 0x00, 0x58, 0x6f, 0x0c, 0x0a, 0x76, 0xe0,
    0xb2, 0x3c, 0x5e, 0x79, 0x00, 0xb5, 0x87, 0x04, 0x0a, 0xa0, 0x43, 0x7a, 0x14, 0x38, 0x46, 0xcb,
    0xec, 0x90, 0xa4, 0xe5, 0x04, 0xdc, 0xa2, 0xd0, 0x5f, 0xef, 0x18, 0x0a, 0x00, 0x00, 0x00, 0x59,
    0xa6, 0xc0, 0x6a, 0x9d, 0xd2, 0x5d, 0x2a, 0xc7, 0xef, 0xa3, 0x9a, 0x4c, 0x77, 0xc9, 0x48, 0x62,
    0x7f, 0x43, 0x08, 0x48, 0x4e, 0x9e, 0xf6, 0x8a, 0x97, 0xc0, 0xcc, 0xd9, 0x76, 0xed, 0x3a, 0xbd,
    0xc7, 0xa3, 0x88, 0xd9, 0x92, 0xff, 0x11, 0xd0, 0x2d, 0x02, 0xe3, 0x0d, 0xa4, 0x7a, 0x58, 0x1c,
    0x8b, 0xce, 0xd0, 0x53, 0x95, 0x05, 0x52, 0x5d, 0x2a, 0xfc, 0x8c, 0xc6, 0x62, 0x43, 0xc8, 0x59,
    0x00, 0x00, 0x00, 0x6c, 0xb4, 0x1b, 0xbe, 0xac, 0x59, 0x7a, 0xba, 0xf8, 0x79, 0x68, 0xd0, 0x33,
    0x6e, 0x17, 0x39, 0x2c, 0x35, 0x8a, 0x21, 0x89, 0xcd, 0xf4, 0x9b, 0xca, 0x32, 0x7f, 0x11, 0x45,
    0xf7, 0x44, 0xdc, 0x8a, 0x66, 0xa6, 0x13, 0x2b, 0x20, 0xb1, 0xd4, 0x9b, 0x8f, 0x1d, 0x13, 0x79,
    0xf5, 0x1d, 0xa1, 0x45, 0xee, 0xe0, 0xe8, 0xbb, 0x85, 0x41, 0x52, 0xca, 0x03, 0xb6, 0xe2, 0xc7,
    0xdb, 0x65, 0x9f, 0xf2, 0x00, 0x00, 0x00, 0x59, 0xb7, 0x93, 0xc5, 0x4f, 0xc1, 0xd0, 0xb6, 0x2e,
    0x96, 0x33, 0xef, 0x49, 0xdf, 0xa3, 0x18, 0xdf, 0xa8, 0x95, 0x5d, 0xc1, 0x21, 0xa7, 0x82, 0x84,
    0x81, 0x28, 0x84, 0xfd, 0xfc, 0x2e, 0x87, 0x82, 0x03, 0x11, 0x68, 0xe2, 0x2f, 0xc5, 0x19, 0x06,
    0x0c, 0xbe, 0x44, 0x89, 0xb1, 0x0d, 0xec, 0x95, 0x62, 0xed, 0xf8, 0xb9, 0x38, 0xc7, 0xe8, 0x18,
    0x10, 0x97, 0xfc, 0x62, 0xb8, 0xf3, 0x05, 0x02, 0x00, 0x00, 0x00, 0x92, 0xf5, 0x9a, 0x36, 0x2f,
    0xa1, 0x83, 0x1f, 0x29, 0x64, 0xac, 0x96, 0x01, 0x27, 0x9a, 0xe2, 0x67, 0x42, 0x43, 0xcc, 0x89,
    0xf0, 0x33, 0x45, 0x10, 0x7d, 0x93, 0x2f, 0xb6, 0x3f, 0x58, 0x5a, 0xcf, 0x7b, 0xbf, 0x20, 0x6b,
    0x45, 0x16, 0xbc, 0x22, 0x51, 0xda, 0x6f, 0xaf, 0xec, 0xe0, 0x32, 0xc9, 0x7e, 0x8d, 0xae, 0x16,
    0xdc, 0x8b, 0xc8, 0xd4, 0xef, 0xcf, 0x37, 0xf0, 0x4a, 0xed, 0x7d, 0xa2, 0x00, 0x00, 0x00, 0x59,
    0x01, 0x05, 0x10, 0x17, 0xdb, 0x36, 0x4c, 0x2e, 0x47, 0x6c, 0x0d, 0x5a, 0xb2, 0x5b, 0x6f, 0x86,
    0x0a, 0xc6, 0x87, 0x00, 0x05, 0x20, 0x67, 0x24, 0x42, 0xab, 0xd4, 0x5c, 0x70, 0xc7, 0x87, 0xd3,
    0xd0, 0xba, 0x99, 0x43, 0x98, 0x9e, 0xa2, 0x99, 0x3b, 0x9d, 0xaf, 0x1b, 0x7f, 0xa9, 0x7a, 0xea,
    0xcd, 0xea, 0x94, 0xbc, 0x94, 0xab, 0x00, 0x05, 0x20, 0x30, 0x64, 0x69, 0x7c, 0x4e, 0xec, 0x07,
    0x6e, 0x0f, 0x02, 0x26, 0x2b, 0x31, 0xc9, 0xab, 0x90, 0x40, 0x6e, 0x90, 0x56, 0x00, 0xc9, 0x27,
    0xbe, 0x2c, 0xdd, 0x5b, 0x96, 0x8f, 0x88, 0xca, 0xc6, 0x01, 0x05, 0x10, 0xd1, 0xa5, 0x0e, 0x0c,
    0xb0, 0x64, 0x19, 0x33, 0xc0, 0xbc, 0x3f, 0x1f, 0x61, 0x72, 0xca, 0x23, 0x05, 0x05, 0x10, 0x89,
    0x0f, 0xfb, 0x6e, 0x08, 0xb2, 0xe7, 0xbe, 0x64, 0x0c, 0xb3, 0xdb, 0x52, 0x88, 0xf6, 0x50, 0x05,
    0x05, 0x10, 0x4e, 0x17, 0x98, 0x59, 0x53, 0x2d, 0x2b, 0x3d, 0xf3, 0x1c, 0x81, 0xfb, 0x5c, 0x3f,
    0x18, 0x5b, 0x05, 0x05, 0x10, 0x4c, 0x78, 0xd6, 0xb7, 0x40, 0x52, 0x4a, 0xee, 0xcf, 0x36, 0x42,
    0xa0, 0xef, 0x66, 0xcc, 0xb1
};

/* Compile with
 *  c++ -o test -L$MBEDDIR/lib -lmbedcrypto -I$MBEDDIR/include -I. -DTEST=1 REST/ApprovalAPI.cpp
 */

int main(int argc, char ** argv) {
    ApprovalAPI api;
    if (argc < 2) {
        fprintf(stderr,"Syntax: %s <tag> [tag ...]", argv[0]);
        exit(1);
    }
    assert(api.parse(testfile, sizeof(testfile)));
    for(int i = 1; i < argc; i++) {
        const char * tag = argv[i];
        ApprovalEntry * e = api.getEntry(tag);
        if (e)
            std::cout << e->name << "\n";
        else
            std::cout << "TAG NOT FOUND" << "\n";
    }
}
#endif
