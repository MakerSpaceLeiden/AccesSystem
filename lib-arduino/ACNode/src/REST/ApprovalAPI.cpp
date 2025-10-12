#include "REST/ApprovalAPI.h"

#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <assert.h>
#include <FS.h>
#include <SPIFFS.h>

#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>

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
    f.close();

    if ((len ==  f.read((uint8_t*)tmp,len)) && import(tmp,len))
	return;  // Import takes over responsibility for the malloced buffer

    Log.println("Cache loading failed");
    free((void*)tmp);
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
        interval = esp_random() & 0xFFF; // about 4 seconds max
    else
        interval = (50 + (esp_random() & 0xF)) * 1000; // 50 seconds + 16-seconds random slack
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
            interval =  (3600  + (esp_random() & 0xFF -128)) * 1000; 
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
    update_t ret = FAIL;
    unsigned long cntr;

    int n = _restAPI->get(url,&len,&buff);
    if (n < 2)
	goto exit;

    buff[n-1] = 0; // damages last byte (CR/LF or comments) - which is ok as we own this buffer
    cntr = atoi((char *)buff);

    if  (getIdentifier()== cntr) 
	    Log.printf("TagDB identifier: %08x: no changes\n", cntr);
    else
	    Log.printf("TagDB identifier: %08x: CHANGED (previous: %08x)\n", cntr, getIdentifier());
 
    last_update = millis();
    
    ret = (cntr != getIdentifier()) ? NEEDS_UPDATE : NO_UPDATE_NEEDED;
exit:
    if (buff) free(buff);
    return ret;
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
        goto exit;
    };

    // Note: import will claim the buffer and manage it.
    if (import(buff,len)) {
        writeCache();
	return;
    };

    Log.println("Failed to import bintags");
exit:
    if (buff) free((void*)buff);
    return;
}

void ApprovalAPI::report(JsonObject& report) {
    report["bintag_id"] = getIdentifier();
    report["bintag_ntags"] = getNumberOfTags();
    char buff[32] = "never";
    
    if (getDataDate()) {
        time_t datadate = getDataDate();
        strncpy(buff, ctime((const time_t *) &datadate),sizeof(buff)-1);
        buff[24]='\0'; // strip \n
    };
    report["bintag_date"] = String(buff);
};

bool ApprovalAPI::canApprove() {
    return blob ? true : false;
}

void ApprovalAPI::sendBestEffortTagApproved(String tag) {
    unsigned char buff[32]; // experting (and ignoring) an simple OK/ERROR or unfound reply
    unsigned char * p = buff;

    char url[256], argtmp[64];
    size_t len = sizeof(buff);

    snprintf(url,sizeof(url), ACL_URL PATH_RECORDUSE "/%s", _argencode(argtmp,sizeof(argtmp),machine));
    String postarg = "tag=" + tag;

    int n = _restAPI->get(url,&len,&p,postarg);
    Debug.printf("Reporting use: %s\n", n < 0 ? "ERR" : String(p,len));
    return;
}

void ApprovalDeck::render_pane(bool refresh) {
    if(!refresh)
        return;
    
    _display->print_centred("TAG DB");
    if (!_approvalAPI) {
        _display->printf("not ready");
        return;
    };
    _display->printf("ID   :%08x %s\n",
                     _approvalAPI->getIdentifier(),
                     _approvalAPI->versionStr());
    
    time_t datadate = _approvalAPI->getDataDate();
    struct tm * t = gmtime((const time_t *)&datadate);
    char ds[12], ts[12];
    time_t age = time(NULL) - _approvalAPI->getDataDate();

    strftime(ds,sizeof(ds),"%Y-%m-%d",t);
    strftime(ts,sizeof(ts),"%H:%M:%S",t);
    _display->printf("Dated:%s\n",ds);
    _display->printf("      %sZ\n",ts);
    _display->printf("Age  :%s\n",since(age));
    _display->printf("Check:%s %s\n", _approvalAPI->getLastUpdate()?
                     since((millis() - _approvalAPI->getLastUpdate())/1000) : "never",
		     _approvalAPI->getLastUpdate()? "ago" : "");
    _display->printCmdBar("UPDATE","NEXT");
};
