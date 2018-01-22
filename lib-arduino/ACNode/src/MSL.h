#ifndef _H_MSL
#define _H_MSL

#include <ACBase.h>

class MSL : public ACSecurityHandler {
   acauth_result_t verify(const char * line, const char ** payload);
   const char * secure(const char * line);
   const char * cloak(const char * tag);
} ;
#endif

