#include "SIG1.h"


// Note - none of below HMAC utility functions is re-entrant/t-safe; they all rely
// on some private static buffers one is not to meddle with in the 'meantime'.
//
static const char q2c[] = "0123456789abcdef"; // Do not 'uppercase' -- the HMACs are calculated over it - and hence are case sensitive.

const char * hmacToHex(const unsigned char * hmac) {
  static char hex[2 * HASH_LENGTH + 1];
  char * p = hex;

  for (int i = 0; i < HASH_LENGTH; i++) {
    *p++ = q2c[hmac[i] >> 4];
    *p++ = q2c[hmac[i] & 15];
  };
  *p++ = 0;

  return hex;
}


const char * hmacAsHex(const char *passwd, const char * beatAsString, const char * topic, const char *payload)
{
  //  const unsigned char * hmac = hmacBytes(passwd, beatAsString, topic, payload);
  // const unsigned char * hmacBytes(const char *passwd, const char * beatAsString, const char * topic, const char *payload) {
  SHA256 sha256;

  sha256.reset();
  sha256.update((unsigned char*)&beatAsString, strlen(beatAsString));
  if (topic && *topic) sha256.update(topic, strlen(topic));
  if (payload && *payload) sha256.update(payload, strlen(payload));

  unsigned char result[sha256.hashSize()];
  sha256.finalizeHMAC(sessionkey, sizeof(sessionkey), result, sizeof(result));

  return hmacToHex(result);
}

void hmac_sign(char * msg, size_t len, const char * beat, const char * payload) {
  const char * vs = "1.0";
  char * topic  = NULL;
  const char * sig  = hmacAsHex(passwd, beat, topic, payload);
  snprintf(msg, len, "SIG/%s %s %s %s", vs, sig, beat, payload);
}

bool hmac_valid(const char * hmac, const char *password, const char * beat, const char *topic, const char *payload) {
  const char * hmac2 = hmacAsHex(passwd, beat, topic, payload);

  if (strcasecmp(hmac2, hmac)) {
    Log.println("Invalid HMAC signature - ignoring.");
    return false;
  }

  return verify_beat(beat);
}


