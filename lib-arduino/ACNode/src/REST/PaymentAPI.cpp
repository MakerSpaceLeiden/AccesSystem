#include "REST/PaymentAPI.h"

#include <ArduinoJson.h>
#include <SPIFFS.h>

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


class SHA256Writer {
public:
  SHA256Writer() {
    mbedtls_sha256_init(&_sha_ctx);
    mbedtls_sha256_starts(&_sha_ctx, 0);
  };
  ~SHA256Writer() {
    mbedtls_sha256_free(&_sha_ctx);
  };
  size_t write(uint8_t c) {
    mbedtls_sha256_update(&_sha_ctx, &c, 1);
    return 1;
  }
  size_t write(const uint8_t *buffer, size_t length) {
    mbedtls_sha256_update(&_sha_ctx, buffer, length);
    return length;
  }
  const uint8_t * sha256() {
    if (!_finished)
	    mbedtls_sha256_finish(&_sha_ctx, _sha256);
    _finished = true;
    return (const uint8_t *)_sha256;
  };
private:
    mbedtls_sha256_context _sha_ctx;
    uint8_t _sha256[32];
    bool _finished = false;
};

bool PaymentAPI::pay(const char *tag, double amount, const char *lbl) {
    char desc[256];
    safesnprintf(desc, sizeof(desc), "%s. Paid at %s", lbl, _restAPI->stationname().c_str());
    
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
 
    if (!res["pricelist"]) {
        Log.println("No pricelist in reply from server");
        return false;
    };
    return parsePricelist(res, true);
}

bool PaymentAPI::parsePricelist(JsonDocument &res, bool cache) {
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

    SHA256Writer sha256Writer;
    serializeJson(res, sha256Writer);

    if (cache) {
          if (!memcmp(sha256Writer.sha256(), _sha256, 256/8)) {
                Log.printf("Pricelist fetched, %d entries -- no changes\n", pricelistlen);
                return true;
          };

          Log.printf("Pricelist fetched, %d entries -- was updated, caching\n", pricelistlen);
      
          File f = SPIFFS.open(PRICELISTFILE, "w");
          if (!f) {
              Log.println("Failed to open product cache file for writing");
              return true;
          }
          int r = serializeJson(res, f);
          f.close();
      
          if (r <= 0) {
              Log.println("Failed to write product cache file ");
              return true;
          }
    };

    // We also keep the sha256 when we are not caching; as that
    // is the case when we got it from the cache to begin with,
    // and we want to avoid writing it out needlesslu.
    //
    memcpy(_sha256, sha256Writer.sha256(), sizeof(_sha256));
    return true;
}

bool PaymentAPI::readCache() {
    memset(_sha256,0,32);
    File f = SPIFFS.open(PRICELISTFILE, "r");
    if (!f) {
        Log.println("Failed to read product cache file ");
        return true;
    };
    JsonDocument res;
    DeserializationError r = deserializeJson(res, f);
    f.close();

    if (r != DeserializationError::Ok) {
        Log.printf("Failed to parse product cache file: %s\n", r.c_str());
        return false;
    };

    return parsePricelist(res, false);
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

void PaymentAPI::report(JsonObject report) {
    report["payment"] = ready();
    report["pricelist"] = pricelist ? pricelist->items.size() : 0;
    if (pricelist)
    	report["pricelist_age"] = (millis() - _lastPricelist)/1000;
}
