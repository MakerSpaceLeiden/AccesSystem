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
    Pricelist();
    std::list<SKU> items;
    SKU defaultItem;
};

class PaymentAPI {
public:
    PaymentAPI(RestAPI * restAPI) : _restAPI(restAPI) {};
    
    // Historic side effect - also sets station name
    bool fetchPricelist();
    
    // Simple, one off, payments
    bool pay(const char * againstTag, double amount, const char * description);
    
    // Claim up to a certain amount. If not settled - it will either be auto
    // settled or left to a human administrator (of settleAfterOrNone == 0).
    //
#define DO_NOT_AUTO_SETTLE (0)
    char * claim(const char * againstUserID,
                 double amount,
                 const char * description,
                 unsigned long settleSecondsAfterOrNot = DO_NOT_AUTO_SETTLE);
    char * update(const char * againstUserID,
                  double amount,
                  const char * description,
                  const char comment = NULL);
    bool settle(char * claim,
                double finalAamount,
                const char * finalDescription,
                const char comment = NULL);
    
private:
    double amount_no_ok_needed = AMOUNT_NO_OK_NEEDED;
    RestAPI * _restAPI;
    Pricelist pricelist;
};
#endif
