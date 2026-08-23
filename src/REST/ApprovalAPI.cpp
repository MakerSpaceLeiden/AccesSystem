#include "REST/ApprovalAPI.h"
#include "Display/Display.h"

#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <assert.h>
#include <SPIFFS.h>
#include <FS.h>

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

bool ApprovalAPI::import(const char * filename) {
    File f = SPIFFS.open(filename,"r");
    return f ? ApprovalBINFile::import(&f) : false;
}

void ApprovalAPI::begin() {
    prepareCache(false);
    _valid = import(TAGBINFILE);
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
            interval =  (3600  + ((esp_random() & 0xFF) -128)) * 1000; 
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
	    Log.printf("TagDB identifier: %08lx: no changes\n", cntr);
    else
	    Log.printf("TagDB identifier: %08lx: CHANGED (previous: %08lx)\n", cntr, getIdentifier());
 
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

    SPIFFS.remove(TAGBINFILE_NEW); // just in case - do not check for errors.

    File f = SPIFFS.open(TAGBINFILE_NEW, "w");
    if (!f) {
        Log.println("Failed to open temp file for writing"); 
        return;
    }
    bool ret = _restAPI->getStream(url, "", &f);
    f.close();

    if (!ret) {
        Log.println("Failed to fetch bintags");
	return;
    };

    _valid = false;
    if (!import(TAGBINFILE_NEW)) {
        Log.println("Failed to import, attempt fallback to old");
	_valid = import(TAGBINFILE);
        return;
    };

    SPIFFS.remove(TAGBINFILE_OLD);
    SPIFFS.rename(TAGBINFILE,TAGBINFILE_OLD);
    SPIFFS.rename(TAGBINFILE_NEW,TAGBINFILE);

    Log.println("Updated tag db");
    _valid = true;

    return;
}

ApprovalEntry * ApprovalAPI::getEntry(const char * tag) {
    if (!_valid) {
	Log.println("Cannot yet approve - no tag file\n");
	return NULL;
    };
    File f = SPIFFS.open(TAGBINFILE,"r");
    if (!f) {
	Log.println("Cannot yet approve - could not open tag file\n");
	return NULL;
    };
    return ApprovalBINFile::getEntry(&f,tag); // we rely on the destructor to close the file.
}

void ApprovalAPI::status(JsonObject & out) {
    // JsonObject r = report["bintags"].to<JsonObject>();
    // r["id"] = getIdentifier();
    report(out);
};

void ApprovalAPI::report(JsonObject & report) {
    JsonObject r = report["bintags"].to<JsonObject>();

    char buff[32] = "never";
    if (getDataDate()) {
        time_t datadate = getDataDate();
        strncpy(buff, ctime((const time_t *) &datadate),sizeof(buff)-1);
        buff[24]='\0'; // strip \n
    };

    r["bintag_date"] = buff;
    r["id"] = getIdentifier();
    r["ntags"] = getNumberOfTags();
    r["version"] = versionStr();
};

bool ApprovalAPI::canApprove() {
    return _valid;
}

void ApprovalAPI::sendBestEffortTagApproved(const char * tag) {
    unsigned char buff[32]; // experting (and ignoring) an simple OK/ERROR or unfound reply
    size_t len = sizeof(buff);
    unsigned char * p = buff;

    char url[128], argtmp[64];

    snprintf(url,sizeof(url), ACL_URL PATH_RECORDUSE "/%s", _argencode(argtmp,sizeof(argtmp),machine));
    snprintf(argtmp, sizeof(argtmp), "tag=%s", tag);

    int n = _restAPI->get(url,&len,&p,argtmp);
    if (n >= 0) p[len] = '\0';
    Debug.printf("Reporting use: %s (%d)\n", (n < 0) ? "ERR" : (char *)p, len);
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
    _display->printf("ID   :%08lx %s\n",
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
    _display->printf("Age  :%s\n",since(age).c_str());
    _display->printf("Check:%s %s\n", (_approvalAPI->getLastUpdate()) ?
                     since((millis() - _approvalAPI->getLastUpdate())/1000).c_str() : "never",
		    (_approvalAPI->getLastUpdate()) ? "ago" : "");
    _display->printCmdBar("UPDATE","NEXT");
};
