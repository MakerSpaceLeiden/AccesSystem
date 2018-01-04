
// Note - none of below HMAC utility functions is re-entrant/t-safe; they all rely
// on some private static buffers one is not to meddle with in the 'meantime'.
//
const char * hmacToHex(const unsigned char * hmac) {
  static char hex[2 * HASH_LENGTH + 1];
  const char q2c[] = "0123456789abcdef"; // Do not 'uppercase' -- the HMACs are calculated over it - and hence are case sensitive.
  char * p = hex;

  for (int i = 0; i < HASH_LENGTH; i++) {
    *p++ = q2c[hmac[i] >> 4];
    *p++ = q2c[hmac[i] & 15];
  };
  *p++ = 0;

  return hex;
}

const unsigned char * hmacBytes(const char *passwd, const char * beatAsString, const char * topic, const char *payload) {
  // static char hex[2 * HASH_LENGTH + 1];

  Sha256.initHmac((const uint8_t*)passwd, strlen(passwd));
  Sha256.print(beatAsString);
  if (topic && *topic) Sha256.print(topic);
  if (payload && *payload) Sha256.print(payload);

  return Sha256.resultHmac();
}

const char * hmacAsHex(const char *passwd, const char * beatAsString, const char * topic, const char *payload)
{
  const unsigned char * hmac = hmacBytes(passwd, beatAsString, topic, payload);
  return hmacToHex(hmac);
}

void hmac_sign(char * msg, size_t len, const char * beat, const char * payload) {
  const char * vs = "1.0";
  const char * sig  = hmacAsHex(passwd, beat, topic, payload);
  snprintf(msg, len, "SIG/%s %s %s %s", vs, sig, beat, payload);
}

bool hmac_valid(const char *password, const * beat, const char *topic, const char *payload) {
  const char * hmac2 = hmacAsHex(passwd, beat, topic, payload);

  if (strcasecmp(hmac2, sig)) {
    Log.println("Invalid HMAC signature - ignoring.");
    return false;
  }

  return verify_beat(beat))
}


