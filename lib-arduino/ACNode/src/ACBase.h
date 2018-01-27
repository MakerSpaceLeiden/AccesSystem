#ifndef _H_ACBASE
#define _H_ACBASE

#include <list>
#include <stddef.h>

class ACBase {
  public:
    const char * name = NULL;

    typedef enum cmd_results { CMD_DECLINE, CMD_CLAIMED } cmd_result_t;

    // virtual void begin(ACNode &acnode);

    void begin() { return; };
    void loop() { return; };
    cmd_result_t handle_cmd(char * cmd, char * rest) { return CMD_DECLINE; };
    
    void set_debug(bool debug);
  protected:
    bool _debug;
  // protected:
};


class ACSecurityHandler : public ACBase {
  public:
    const char * name = NULL;
    
   typedef enum acauth_results { DECLINE, FAIL, PASS, OK } acauth_result_t;

   acauth_result_t verify(const char * topic, const char * line, char ** payload) { return FAIL; }

   const char * secure(const char * topic,  const char * line) { return NULL; };

   const char * cloak(const char * tag) { return NULL; };
};
#endif
