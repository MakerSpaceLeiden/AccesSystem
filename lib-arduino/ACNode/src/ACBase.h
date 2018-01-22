#ifndef _H_ACBASE
#define _H_ACBASE

#include <list>
#include <stddef.h>

class ACBase {
  public:
    // virtual void begin(ACNode &acnode);
    virtual void begin() { return; };
    virtual void loop() { return; };
    void set_debug(bool debug);
  protected:
    bool _debug;
  // protected:
};


class ACSecurityHandler : public ACBase {
  public:
   typedef enum acauth_results { DECLINE, FAIL, OK } acauth_result_t;

   virtual acauth_result_t verify(const char * line, const char ** payload) { return FAIL; }

   virtual const char * secure(const char * line) { return NULL; };

   virtual const char * cloak(const char * tag) { return NULL; };
};
#endif
