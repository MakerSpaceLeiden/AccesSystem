#include "ACNode.h"
#include "REST/RestAPI.h"

// Note: this thin wedge is quite expensive in terms of memory.
//       around a 160k when empty.
//

#if 0
class ACL {
public:
    ACL(String tag, String name, bool permit) : tag(tag), name(name), permit(permit);
    String tag, name;
    bool permit;
    unsigned char needs, has;
    };
#endif
    
class ACNodeRest : public ACNodeBase {
private:
    typedef ACNodeBase super;

public:
    ACNodeRest(const char * machine, const char * ssid, const char * ssid_passwd);
    ACNodeRest(const char * machine = NULL, bool wired = true);

    void CONSTS();
    void pop();
    
    void begin(eth_board_t board = BOARD_AART, uint8_t clear_button = -1);
    void loop();

    void request_approval(const char * tag, const char * operation = NULL, const char * target = NULL, bool useCacheOk= true);

protected:
    RestAPI _restAPI;
    
private:
//    std::list<ACL> list;
};
