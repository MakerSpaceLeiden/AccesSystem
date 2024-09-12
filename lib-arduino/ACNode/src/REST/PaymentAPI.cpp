#include "REST/PaymentAPI.h"
#include <ArduinoJSON.h>
#include "util/common-utils.h"
#include "rest.h"

#define PAY_PATH "/v2/pay"

bool PaymentAPI::pay(const char *tag, double amount, const char *lbl) {
    char buff[512];
    char desc[256];
    char tmp[256];
    char * encarg;
    
    snprintf(desc, sizeof(desc), "%s. Paid at %s", lbl, _restAPI->stationname());
    
    encarg =_argencode(tmp, sizeof(tmp), desc);
    if (!encarg) {
        Log.println("PaymentAPI::pay tmp buffer too small for agrumens.");
        return false;
    };
    if (0) {
        // avoid logging the tag for privacy/security-by-obscurity reasons.
        //
        snprintf(buff, sizeof(buff), PAY_URL PAY_PATH "?node=%s&src=%s&amount=%s&description=%s",
                 _restAPI->stationname(), "XX-XX-XX-XXX", amount, encarg);
        Log.print((const char*)"URL : ");
        Log.println(buff);
    };
    snprintf(buff, sizeof(buff), PAY_URL PAY_PATH "?node=%s&src=%s&amount=%f&description=%s",
             _restAPI->stationname(), tag, amount, encarg);
    
    JsonDocument res = _restAPI->get(buff);
    return res["result"].as<bool>();
}

bool PaymentAPI::fetchPricelist() {
    JsonDocument res = _restAPI->get(PAY_URL REGISTER_PATH);
    
    if (!res["pricelist"]) {
        Log.println("No pricelist received");
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
    
    pricelist = Pricelist();
    for (JsonVariant item : arr) {
        
        SKU sku(item["amount"],item["price"],item["desc"]);
        pricelist.items.push_back(sku);
        
        if (item["default"])
            pricelist.defaultItem = sku;
    };
    return true;
}
