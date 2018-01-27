#include <MSL.h>
#include <string.h>

// Return 
//	PASS	ignored - not my cup of tea.
//	FAIL	failed to authenticate - reject it.
//	OK	authenticated OK - accept.
//
ACSecurityHandler::acauth_result_t MSL::verify(const char * topic, const char * line, const char ** payload) {
	// We only accept short, single word commands.

        if (strlen(line) > 10 || index(line,' '))
		return ACSecurityHandler::DECLINE;

    *payload = line;
	return ACSecurityHandler::OK;
};

const char * MSL::secure(const char * line) {
	return line;
};

const char * MSL::cloak(const char * tag) {
	return tag;
};

