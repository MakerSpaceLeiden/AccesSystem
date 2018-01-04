// Curve/Ed25519 related (and SIG/2.0 protocol)
#include "SIG2.h"


#if HASH_LENGTH != CURVE259919_SESSIONLEN
#error SHA256 "hash HASH_LENGTH should be the same size as the session key CURVE259919_SESSIONLEN"
#endif

#if HASH_LENGTH != 32 // AES256::keySize() 
#error SHA256 "hash should be the same size as the encryption key"
#endif

eeprom_t eeprom;

uint8_t node_publicsign[CURVE259919_KEYLEN];

// Curve25519 key (In montgomery x space) - not kept in
// persistent storage as we renew on reboot in a PFS
// sort of 'light' mode.
//
uint8_t node_publicsession[CURVE259919_KEYLEN];
uint8_t node_privatesession[CURVE259919_KEYLEN];
uint8_t sessionkey[CURVE259919_SESSIONLEN];

bool sig2_active() {
  return (eeprom.flags & CRYPTO_HAS_PRIVATE_KEYS);
}

void kickoff_RNG() {
  // Attempt to get a half decent seed soon after boot. We ought to pospone all operations
  // to the run loop - well after DHCP has gotten is into business.
  //
  // Note that Wifi/BT should be on according to:
  //    https://github.com/espressif/esp-idf/blob/master/components/esp32/hw_random.c
  //
  RNG.begin(RNG_APP_TAG, RNG_EEPROM_ADDRESS);

  SHA256 sha256;
  sha256.reset();

  for (int i = 0; i < 25; i++) {
    uint32_t r = trng(); // RANDOM_REG32 ; // Or esp_random(); for the ESP32 in recent libraries.
    sha256.update((unsigned char*)&r, sizeof(r));
    delay(10);
  };
  uint8_t result[sha256.hashSize()];
  sha256.finalize(result, sizeof(result));
  RNG.stir(result, sizeof(result), 100);

  RNG.setAutoSaveTime(60);
}

void maintain_rng() {
  RNG.loop();

  if (RNG.available(1024 * 4))
    return;
  uint32_t seed = trng();
  RNG.stir((const uint8_t *)&seed, sizeof(seed), 100);
}

#define EEPROM_PRIVATE_OFFSET (0x100)
void load_eeprom() {
  for (size_t adr = 0; adr < sizeof(eeprom); adr++)
    ((uint8_t *)&eeprom)[adr] = EEPROM.read(EEPROM_PRIVATE_OFFSET + adr);
}

void save_eeprom() {
  for (size_t adr = 0; adr < sizeof(eeprom); adr++)
    EEPROM.write(EEPROM_PRIVATE_OFFSET + adr,  ((uint8_t *)&eeprom)[adr]);
  EEPROM.commit();
}

void wipe_eeprom() {
  bzero((uint8_t *)&eeprom, sizeof(eeprom));
  eeprom.version = EEPROM_VERSION;
  save_eeprom();
}

void init_curve() {
  EEPROM.begin(1024);
  load_eeprom();

  if (eeprom.version != EEPROM_VERSION) {
    Log.printf("EEPROM Version %04x not understood -- clearing.\n", eeprom.version );
    wipe_eeprom();
  }
}
// Ideally called from the runloop - i.e. late once we have at least a modicum of
// entropy from wifi/etc.
//
int setup_curve25519() {
  if (eeprom.flags & CRYPTO_HAS_PRIVATE_KEYS) {
    resetWatchdog();
    Ed25519::derivePublicKey(node_publicsign, eeprom.node_privatesign);
  }

  if (millis() - laststatechange < 1000)
    return -1;

  Log.println("Generating Curve25519 session keypair");

  resetWatchdog();
  Curve25519::dh1(node_publicsession, node_privatesession);
  bzero(sessionkey, sizeof(sessionkey));

  if (eeprom.flags & CRYPTO_HAS_MASTER_TOFU) {
    Debug.printf("EEPROM Version %04x contains all needed keys and is TOFU to a master with public key\n", eeprom.version);
    return 0;
  }

  resetWatchdog();
  Ed25519::generatePrivateKey(eeprom.node_privatesign);
  resetWatchdog();
  Ed25519::derivePublicKey(node_publicsign, eeprom.node_privatesign);

  eeprom.flags |= CRYPTO_HAS_PRIVATE_KEYS;

  save_eeprom();
  return 0;
}


// Option 1 (caninical) - a (correctly) signed message with a known key.
// Option 2 - a (correctly) signed message with an unknwon key and still pre-TOFU
//            we need to check against the key passed rather than the one we know.
// Option 3 - a (correctly) signed message with an unknwon key - which is not the same as the TOFU key
// Option 4 - a (incorrectly) signed message. Regardless of TOFU state.


bool sig2_verify(const char * beat, const char signature64[], const char signed_payload[]) {
#define B64L(n) ((((4 * n / 3) + 3) & ~3)+1)
  char master_publicsignkey_b64[B64L(CURVE259919_KEYLEN )];
  char master_publicencryptkey_b64[B64L(CURVE259919_KEYLEN)];

  uint8_t pubsign_tmp[CURVE259919_KEYLEN];
  uint8_t pubencr_tmp[CURVE259919_SESSIONLEN];

  uint8_t signature[ED59919_SIGLEN];

  bool tofu = (eeprom.flags & CRYPTO_HAS_MASTER_TOFU) ? true : false;
  bool newsession = false;
  uint8_t * signkey = NULL; // tentative signing key in case of tofu
  bool newtofu = false;

  B64D(signature64, signature, "Ed25519 signature");
  char * p = index(signed_payload, ' ');
  while (*p == ' ') p++;
  p = index(p, ' ');
  if (!p || !*p || *++p) {
    Log.println("Maformed payload; no command");
    return false;
  };

  char * q = index(p, ' ');
  size_t cmd_len = (q && *q) ? q - p : strlen(p + 1);
  char cmd[cmd_len + 1];
  cmd[cmd_len] = '\0';
  strncpy(cmd, p, cmd_len);

  if (strcmp(cmd, "welcome") == 0  || strcmp(cmd, "announce") == 0) {
    newsession = true;

    SEP(host_ip, "IP address", false);
    SEP(master_publicsignkey_b64, "B64 public signing key", false);
    SEP(master_publicencryptkey_b64, "B64 public encryption key", false);

    B64D(master_publicsignkey_b64, pubsign_tmp, "Ed25519 public key");
    B64D(master_publicencryptkey_b64, pubencr_tmp, "Curve25519 public key");

    if (tofu) {
      if (memcmp(eeprom.master_publicsignkey, pubsign_tmp, sizeof(eeprom.master_publicsignkey))) {
        Log.println("Sender has changed its public signing key(s) - ignoring.");
        return false;
      }
      Debug.println("Known Ed25519 signature on message.");
    } else {
      Debug.println("Unknown Ed25519 signature on message - giving the benefit of the doubt.");
      signkey = pubsign_tmp;

      // We are not setting the TOFU flag in the EEPROM yet, as we've not yet checked
      // for reply by means of the beat.
      //
      newtofu = true;
    }

    if (!tofu && !newtofu) {
      Log.println("Cannot (yet) validate signature - ignoring while waiting for welcome/announce");
      return false;
    };
  };

  resetWatchdog();
  if (!Ed25519::verify(signature, signkey, signed_payload, strlen(signed_payload))) {
    Log.println("Invalid Ed25519 signature on message - ignoring.");
    return false;
  };

  if (!verify_beat(beat))
    return false;

  if (newtofu) {
    Debug.println("TOFU for Ed25519 key of master, stored in persistent store..");
    memcpy(eeprom.master_publicsignkey, signkey, sizeof(eeprom.master_publicsignkey));

    eeprom.flags |= CRYPTO_HAS_MASTER_TOFU;
    save_eeprom();
  }
  if (newsession) {
    // Allways allow for the updating of session keys. On every welcome/announce. Provided that
    // the signature matched.

    // We need to copy the key as 'dh2()' will wipe its inputs as a side effect of the calculation.
    // Which usually makes sense -- but not in our case - as we're async and may react to both
    // a welcome and an announce -- so regenerating it on both would confuse matters.
    //
    // XX to do - consider regenerating it after a welcome; and go through replay attack options.
    //
    uint8_t tmp_private[CURVE259919_KEYLEN];

    memcpy(sessionkey, pubencr_tmp, sizeof(sessionkey));
    memcpy(tmp_private, node_privatesession, sizeof(tmp_private));
    resetWatchdog();
    Curve25519::dh2(sessionkey, tmp_private);

    resetWatchdog();

    SHA256 sha256;
    sha256.reset();
    sha256.update((unsigned char*)&sessionkey, sizeof(sessionkey));
    sha256.finalize(sessionkey, sizeof(sessionkey));

#if 0
    unsigned char key_b64[128];
    encode_base64(sessionkey, sizeof(sessionkey), key_b64);
    Serial.print("RAW session key: "); Serial.println((char *)key_b64);
    encode_base64(sessionkey, sizeof(sessionkey), key_b64);
    Serial.print("HASHed session key: "); Serial.println((char *)key_b64);
#endif

    Log.printf("(Re)calculated session key - slaved to master public signkey %s and masterpublic encrypt key %s\n", master_publicsignkey_b64, master_publicencryptkey_b64);

    eeprom.flags |= CRYPTO_HAS_MASTER_TOFU;
  };
  return true;
}

void sig2_sign(char msg[MAX_MSG], size_t maxlen, const char * tosign) {
  const char * vs = "2.0";
  uint8_t signature[ED59919_SIGLEN];


  resetWatchdog();
  Ed25519::sign(signature, eeprom.node_privatesign, node_publicsign, tosign, maxlen);

  char sigb64[ED59919_SIGLEN * 2]; // plenty for an HMAC and for a 64 byte signature.
  encode_base64(signature, sizeof(signature), (unsigned char *)sigb64);

  snprintf(msg, MAX_MSG, "SIG/%s %s %s", vs, sigb64, tosign);
}


const char * sig2_encrypt(const char * lasttag, char * tag_encoded, size_t maxlen) {
  CBC<AES256> cipher;

  uint8_t iv[16];
  RNG.rand(iv, sizeof(iv));

  if (!cipher.setKey(sessionkey, cipher.keySize())) {
    Log.println("FAIL setKey");
    return NULL;
  }

  if (!cipher.setIV(iv, cipher.ivSize())) {
    Log.println("FAIL setIV");
    return NULL;
  }

  // PKCS#7 padding - as traditionally used with AES.
  // https://www.ietf.org/rfc/rfc2315.txt
  // -- section 10.3, page 21 Note 2.
  //
  size_t len = strlen(lasttag);
  int pad = 16 - (len % 16); // cipher.blockSize();
  if (pad == 0) pad = 16; //cipher.blockSize();

  size_t paddedlen = len + pad;
  uint8_t input[ paddedlen ], output[ paddedlen ], output_b64[ paddedlen * 4 / 3 + 4  ], iv_b64[ 32 ];
  strcpy((char *)input, lasttag);

  for (int i = 0; i < pad; i++)
    input[len + i] = pad;

  cipher.encrypt(output, (uint8_t *)input, paddedlen);
  encode_base64(iv, sizeof(iv), iv_b64);
  encode_base64(output, paddedlen, output_b64);

#if 0
  unsigned char key_b64[128];  encode_base64(sessionkey, sizeof(sessionkey), key_b64);
  Serial.print("Plain len="); Serial.println(strlen(lasttag));
  Serial.print("Paddd len="); Serial.println(paddedlen);
  Serial.print("Key Size="); Serial.println(cipher.keySize());
  Serial.print("IV Size="); Serial.println(cipher.ivSize());
  Serial.print("IV="); Serial.println((char *)iv_b64);
  Serial.print("Key="); Serial.println((char *)key_b64);
  Serial.print("Cypher="); Serial.println((char *)output_b64);
#endif

  snprintf(tag_encoded, sizeof(maxlen), "%s.%s", iv_b64, output_b64);
  return tag_encoded;
}

void send_helo(const char * helo) {
  char buff[MAX_MSG];

  IPAddress myIp = WiFi.localIP();
  snprintf(buff, sizeof(buff), "%s %d.%d.%d.%d", helo, myIp[0], myIp[1], myIp[2], myIp[3]);

  if (sig2_active()) {
    char b64[128];

    // Add ED25519 signing/non-repudiation key
    strncat(buff, " ", sizeof(buff));
    encode_base64((unsigned char *)node_publicsign, sizeof(node_publicsign), (unsigned char *)b64);
    strncat(buff, b64, sizeof(buff));

    // Add Curve25519 session/confidentiality key
    strncat(buff, " ", sizeof(buff));
    encode_base64((unsigned char *)(node_publicsession), sizeof(node_publicsession), (unsigned char *)b64);
    strncat(buff, b64, sizeof(buff));
  }

  Log.println(buff);
  send(NULL, buff);
}
