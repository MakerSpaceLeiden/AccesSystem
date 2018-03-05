#ifndef _H_MSL
#define _H_MSL

#include <ACBase.h>

class MSL : public ACSecurityHandler {
   const char * name = "MSL";
   acauth_result_t verify(ACRequest * rew);
} ;
#endif

