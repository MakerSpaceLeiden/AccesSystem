#include "REST/PaymentAPI.h"
#include <ArduinoJson.h>
#include "util/common-utils.h"
#include "rest.h"

#ifndef PAY_URL
#define PAY_URL "https://my.crm.local:443/pettycash/api"
#endif

#ifndef PAY_PATH
#define PAY_PATH "/pettycash/api/v2"
#endif

#define CLAIM_CREATE_PATH "/claim_create"
#define CLAIM_UPDATE_PATH "/claim_update"
#define CLAIM_SETTLE_PATH "/claim_settle"

#define CLAIM_CREATE_URL PAY_URL PAY_PATH CLAIM_CREATE_PATH
#define CLAIM_UPDATE_URL PAY_URL PAY_PATH CLAIM_UPDATE_PATH
#define CLAIM_SETTLE_URL PAY_URL PAY_PATH CLAIM_SETTLE_PATH

bool PaymentAPI::pay(const char *tag, double amount, const char *lbl) {
    char buff[512];
    char desc[256];
    char tmp[256];
    
    snprintf(desc, sizeof(desc), "%s. Paid at %s", lbl, _restAPI->stationname());
    
    JsonDocument res = _restAPI->get(PAY_URL PAY_PATH,encodeargs({
        "node", String(_restAPI->stationname()),
        "src", String(tag),
        "amount:", String(amount),
        "descrioption",String(desc),
    }, false));
    return res["result"].as<bool>();
}

bool PaymentAPI::fetchPricelist() {
    const char * url = PAY_URL "/v2/register";
    _lastPricelist = millis();

    Debug.printf("Fetching pricelist at %s\n", url);
    JsonDocument res = _restAPI->get(url);
    // serializeJson(res, Debug);
 
    if (!res["pricelist"]) {
        Log.println("No pricelist in reply from server");
        return false;
    };
    
    const char * nme = res["name"];
    if (!nme || !strlen(nme)) {
        Log.println("no station assigned in CRM");
        return false;
    }
    const char * desc = res["description"];
    if (desc && strlen(desc))
        _restAPI->setStationname(desc);
    
    double  cap = res["max_permission_amount"];
    if (cap > 0) {
        Log.printf("Non default permission amount of %.2f euro\n", cap);
        amount_no_ok_needed = cap;
    };
    
    JsonArray arr = res["pricelist"].as<JsonArray>();
    
    int pricelistlen = arr.size();
    if (pricelistlen < 0 || pricelistlen > 256) {
        Log.println("Bogus SKU price list or too large");
        return false;
    }
    
    if (pricelist)
        delete pricelist;
    pricelist = new Pricelist();
    
    for (JsonVariant item : arr) {
        String name = item["name"];
        String desc = item["description"];
        double price = atof(item["price"]);
 
        SKU sku(name,price,desc);
        pricelist->items.push_back(sku);
        
        if (item["default"])
            pricelist->defaultItem =  &(pricelist->items.back());
    };

    Log.printf("Pricelist fetched, %d entries\n", pricelistlen);
    return true;
}

String PaymentAPI::claim(const char * againstUserID,
                         double amount,
                         const char * description,
                         unsigned long settleSecondsAfterOrNot)
{
    std::vector<String> args = {
        "uid", String(againstUserID),
        "amount",String(amount),
        "description",String(description)
    };
    
    if (settleSecondsAfterOrNot != DO_NOT_AUTO_SETTLE) {
        args.push_back("settleInSeconds");
        args.push_back(String(settleSecondsAfterOrNot));
    };
    return _raw_claim(CLAIM_CREATE_URL,args);
}

bool PaymentAPI::update(const char * claim,
                          double amount,
                          const char * description,
                          const char * comment) {
    std::vector<String> args = {
        "claim", String(claim),
        "amount",String(amount),
        "description",String(description ? description : ""),
        "comment",String(comment ? comment : ""),
    };
    String ret =  _raw_claim(CLAIM_UPDATE_URL,args);
    return ret.length() > 0;
}

bool PaymentAPI::settle(const char * claim,
                        double finalAamount,
                        const char * description,
                        const char * comment) {
    std::vector<String> args = {
        "claim", String(claim),
        "amount",String(finalAamount),
        "description",String(description ? description : ""),
        "comment",String(comment ? comment : ""),
    };
    String ret = _raw_claim(CLAIM_SETTLE_URL,args);
    return ret.length() > 0;
}

String PaymentAPI::_raw_claim(const char * url, std::vector<String> args) {
    unsigned char  * buffp = NULL;
    size_t max_len = 64;
    String ret;
    
    String payload = encodeargs(args,true /* strip empty strings */);
    int r = _restAPI->get(url,&max_len,&buffp,payload);
    
    if ((r > 0) && buffp && max_len) {
	ret = String((char*)buffp);
	free(buffp);
    };
    return ret;
}

void PaymentAPI::loop() {
    if (!_needsPricelist)
	return;
    if (!eth_connected()) 
	return;
    if(!ready()) 
	return;
    if ((millis() > _lastPricelist + (pricelist ? 180 : 1) * 60 * 1000) || (_lastPricelist == 0))
    	fetchPricelist();
}

void PaymentAPI::report(JsonObject& report) {
    report["payment"] = ready();
    report["pricelist"] = pricelist ? pricelist->items.size() : 0;
    if (pricelist)
    	report["pricelist_age"] = (millis() - _lastPricelist)/1000;
}
