#include <MSL.h>
#include <string.h>

// Return 
//	PASS	ignored - not my cup of tea.
//	FAIL	failed to authenticate - reject it.
//	OK	authenticated OK - accept.
//
ACSecurityHandler::acauth_result_t MSL::verify(ACRequest *req) {
	// We only accept short, single word commands.
    //
    if (strlen(req->payload) > 10 || index(req->payload,' '))
		return ACSecurityHandler::DECLINE;

	return ACSecurityHandler::OK;
};
