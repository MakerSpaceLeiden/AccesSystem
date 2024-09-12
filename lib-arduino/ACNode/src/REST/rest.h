#ifndef _H_MPN_REST
#define _H_MPN_REST

#include <ArduinoJSON.h>

typedef enum { NOERROR = 0, ERR_RETRYABLE = -1, ERR_FATAL = -2, ERR_REPAIR = -3, NOERROR_OK = 1 } rest_ret_t;


// URL of an https://github.com/MakerSpaceLeiden/makerspaceleiden-crm instance.
//
#ifndef PAY_URL
#define PAY_URL "https://my.crm.local:443/pettycash/api"
#endif

#define NONE_PATH "/none"
#define REGISTER_PATH "/v3/register"

void wipekeys();
rest_ret_t setupAuth(const char * terminalName);

// Protocol
rest_ret_t fetchCA(const char * terminalName);
rest_ret_t checkRegistrationDevice(const char * terminalName);
rest_ret_t registerDevice(const char * terminalName);
rest_ret_t registerDeviceSwipe(const char * terminalName, const char * tag);

size_t raw_rest(const char * terminalName, const char *url, size_t * maxbufflenp, unsigned char ** buffp, rest_ret_t * ret);
JsonDocument raw_rest(const char * terminalName, const char *url, rest_ret_t * ret);

#endif
