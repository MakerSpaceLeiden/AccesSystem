#ifndef _REST_PAYMENT_API
#define _REST_PAYMENT_API
#include <list>

#include "REST/RestAPI.h"

// Up until 5.00 Euro can be approved by a swipe; otherwise
// there is an extra yes/no question (unless the server tells
// us differently by setting a higher cap).
//
#ifndef AMOUNT_NO_OK_NEEDED
#define AMOUNT_NO_OK_NEEDED (5.0)
#endif

// Stock Keeping Unit - smallest thing we can sell in any quantity.
//
class SKU {
public:
    SKU();
    SKU(String n, double p, String description) : name(n), price(p), desc(description) {};
    String name, desc;
    double price;
};

class Pricelist {
public:
    Pricelist() { defaultItem = NULL; };
    std::list<SKU> items;
    SKU * defaultItem;
};

class PaymentAPI {
public:
    PaymentAPI(RestAPI * restAPI) : _restAPI(restAPI) {
        _restAPI->onPaired([&]() -> void {
            // conceivable we could also load the price list here - but
            // not sure if that is always needed ?
            if (_ready_cb)
                _ready_cb();
            _ready = true;
        });
    };

    typedef std::function<void(void)> THandlerFunction_NotifyReady;
    Pricelist * pricelist = NULL;

    PaymentAPI& onReady(THandlerFunction_NotifyReady fn) {_ready_cb = fn; return *this; };
    bool ready() { return _ready; };
    
    // Historic side effect - also sets station name
    bool fetchPricelist();
    
    // Simple, one off, payments
    bool pay(const char * againstTag, double amount, const char * description);
    
    // Claim up to a certain amount. If not settled - it will either be auto
    // settled or left to a human administrator (of settleAfterOrNone == 0).
    //
    // Warning - user is to free claim post use !
#define DO_NOT_AUTO_SETTLE (0)
    char * claim(const char * againstUserID,
                 double amount,
                 const char * description,
                 unsigned long settleSecondsAfterOrNot = DO_NOT_AUTO_SETTLE);
    bool update(const char * claim,
                  double amount,
                  const char * updatedDescriptionIfAny = NULL,
                  const char * comment = NULL);
    bool settle(const char * claim,
                double finalAamount,
                const char * finalDescriptionIfAny = NULL,
                const char * commentIfAny = NULL);

private:
    double amount_no_ok_needed = AMOUNT_NO_OK_NEEDED;
    RestAPI * _restAPI;
    THandlerFunction_NotifyReady _ready_cb;
    bool _ready;
    
    char * _raw_claim(const char * url, std::vector<String> args);
};
#endif
