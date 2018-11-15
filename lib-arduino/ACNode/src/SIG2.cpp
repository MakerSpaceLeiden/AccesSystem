#include <ACNode-private.h>
#include "SIG2.h"
#include "MakerSpaceMQTT.h" // needed for MAX_MSG
#include <Arduino.h> // min() macro
#include <Crypto.h>
#include <Curve25519.h>
#include <Ed25519.h>
#include <RNG.h>
#include <SHA256.h>

#include <CBC.h>
#include <AES256.h>

#include <EEPROM.h>

// Curve/Ed25519 related (and SIG/2.0 protocol)

#define CURVE259919_KEYLEN      (32)
#define CURVE259919_SESSIONLEN  (CURVE259919_KEYLEN)
#define ED59919_SIGLEN          (64)

#if HASH_LENGTH != CURVE259919_SESSIONLEN
#error SHA256 "hash HASH_LENGTH should be the same size as the session key CURVE259919_SESSIONLEN"
#endif

#if HASH_LENGTH != 32 // AES256::keySize()
#error SHA256 "hash should be the same size as the encryption key"
#endif

typedef struct __attribute__ ((packed)) {
#define EEPROM_VERSION (0x0103)
  uint16_t version;
  uint8_t flags;
  uint8_t spare;

  // Ed25519 key (Curve25519 key in Edwards y space)
  uint8_t node_privatesign[CURVE259919_KEYLEN];
  uint8_t master_publicsignkey[CURVE259919_KEYLEN];

} eeprom_t;

extern eeprom_t eeprom;
extern uint8_t node_publicsign[CURVE259919_KEYLEN];

// Curve25519 key (In montgomery x space) - not kept in
// persistent storage as we renew on reboot in a PFS
// sort of 'light' mode.
//
extern uint8_t node_publicsession[CURVE259919_KEYLEN];
extern uint8_t node_privatesession[CURVE259919_KEYLEN];
extern uint8_t sessionkey[CURVE259919_SESSIONLEN];

#define CRYPTO_HAS_PRIVATE_KEYS (1<<0)
#define CRYPTO_HAS_MASTER_TOFU (1<<1)

#ifdef BUILD
#define RNG_APP_TAG BUILD
#else
#define RNG_APP_TAG  __FILE__  __DATE__  __TIME__
#endif

#define RNG_EEPROM_ADDRESS (sizeof(eeprom)+4)

extern bool sig2_active();

extern void kickoff_RNG();

extern void load_eeprom();
extern void save_eeprom();
extern void wipe_eeprom();

// Option 1 (caninical) - a (correctly) signed message with a known key.
// Option 2 - a (correctly) signed message with an unknwon key and still pre-TOFU
//            we need to check against the key passed rather than the one we know.
// Option 3 - a (correctly) signed message with an unknwon key - which is not the same as the TOFU key
// Option 4 - a (incorrectly) signed message. Regardless of TOFU state.

eeprom_t eeprom;

uint8_t node_publicsign[CURVE259919_KEYLEN];

// Curve25519 key (In montgomery x space) - not kept in
// persistent storage as we renew on reboot in a PFS
// sort of 'light' mode.
//
uint8_t node_publicsession[CURVE259919_KEYLEN];
uint8_t node_privatesession[CURVE259919_KEYLEN];
uint8_t sessionkey[CURVE259919_SESSIONLEN];

static int init_done = 0;

bool sig2_active() {
  return (eeprom.flags & CRYPTO_HAS_PRIVATE_KEYS && init_done >= 2);
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

  uint8_t mac[6];
  WiFi.macAddress(mac);
  sha256.update(mac, sizeof(mac));
#if 0
  ETH.macAddress(mac);
  sha256.update(mac, sizeof(mac));
#endif

  uint8_t result[sha256.hashSize()];
  sha256.finalize(result, sizeof(result));
  RNG.stir(result, sizeof(result), 100);

  RNG.setAutoSaveTime(60);
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

// Ideally called from the runloop - i.e. late once we have at least a modicum of
// entropy from wifi/etc.
//
void SIG2::begin() {
  EEPROM.begin(1024);
  load_eeprom();

  if (eeprom.version != EEPROM_VERSION) {
    Log.printf("EEPROM Version %04x not understood -- clearing.\n", eeprom.version );
    wipe_eeprom();
  }
  Serial.println("Got a valid eeprom.");

  Beat::begin();
}

void SIG2::loop() {
  RNG.loop();
  Beat::loop();

  if (init_done == 0) {
    kickoff_RNG();
    init_done = 1;
    return;
  };

  if (!RNG.available(1024 * 4)) {
    unsigned long str = millis();
    for(int i = 0; i < 32  && millis() - str < 200; i++) {
       uint32_t seed = trng();
       RNG.stir((const uint8_t *)&seed, sizeof(seed), 100);
    };
    return;
  };

  if (!_acnode->isConnected())
    return;

  if (init_done > 3)
    return;
  

  if (init_done == 1) {
    Debug.println("Generating Curve25519 session keypair");
    resetWatchdog();
    Curve25519::dh1(node_publicsession, node_privatesession);
    bzero(sessionkey, sizeof(sessionkey));

    if (eeprom.flags & CRYPTO_HAS_PRIVATE_KEYS) {
      Debug.printf("EEPROM Version %04x contains all needed keys and is TOFU to a master with public key\n", eeprom.version);
    } else {
      resetWatchdog();
      Ed25519::generatePrivateKey(eeprom.node_privatesign);

      eeprom.flags |= CRYPTO_HAS_PRIVATE_KEYS;

      save_eeprom();
      Debug.printf("EEPROM Version %04x contains all new private key for TOFU\n", eeprom.version);
    }

    resetWatchdog();
    Ed25519::derivePublicKey(node_publicsign, eeprom.node_privatesign);

    init_done = 2;
    Debug.println("Full init. Ready for crypto");
    return;
  };
  if (init_done == 2 && _acnode->isUp() && sig2_active()) {
    init_done = 3;
    Log.println("SIG/2 ready, connected to mqtt, have private key and am announcing.");
    _acnode->send_helo();
   return;
  };
}

// Note - we're not siging the topic.
//
ACSecurityHandler::acauth_result_t SIG2::verify(ACRequest * req) {
  size_t len = strlen(req->payload);
  char buff[MAX_MSG];

  // We only accept things starting with SIG/2*<space>hex<space>
  if (len < 72 || strncmp(req->payload, "SIG/2.", 6) != 0) 
    return ACSecurityHandler::DECLINE;

  if (len > sizeof(buff) - 1) {
    Debug.println("Failing SIG/2 sigature - far too long");
    return FAIL;
  };

  strncpy(buff, req->payload, sizeof(buff));
  char * p = buff;

  SEP(version, "SIG2Verify failed - no version", ACSecurityHandler::FAIL);
  strncpy(req->version, version, sizeof(req->version));

  SEP(signature64, "SIG2Verify failed - no signature64", ACSecurityHandler::FAIL);

  while (p && *p == ' ') p++;
  strncpy(req->rest, p, sizeof(req->rest));

  SEP(beat, "SIG1Verify failed - no beat", ACSecurityHandler::FAIL);
  strncpy(req->beat, beat, sizeof(req->beat));

  // Annoyingly - not all implementations of base64 are careful
  // with the final = and == and trailing \0.
  //
  if (abs(strlen(signature64) - B64L(ED59919_SIGLEN)) > 3) {
    Debug.printf("Failing SIG/2 sigature - wrong length signature64 (%d,%d)\n",
                 strlen(signature64), B64L(ED59919_SIGLEN));
    return ACSecurityHandler::FAIL;
  };

  req->beatExtracted = strtoul(req->beat, NULL, 10);
  if (req->beatExtracted == 0) {
    Debug.println("Failing SIG/2 sigature - beat parsing failed");
    return ACSecurityHandler::FAIL;
  };

  // Option 1 (caninical) - a (correctly) signed message with a known key.
  // Option 2 - a (correctly) signed message with an unknwon key and still pre-TOFU
  //            we need to check against the key passed rather than the one we know.
  // Option 3 - a (correctly) signed message with an unknwon key - which is not the same as the TOFU key
  // Option 4 - a (incorrectly) signed message. Regardless of TOFU state.

  char master_publicsignkey_b64[B64L(CURVE259919_KEYLEN )];
  char master_publicencryptkey_b64[B64L(CURVE259919_KEYLEN)];

  uint8_t signature[ED59919_SIGLEN];
  B64DE(signature64, signature, "Ed25519 signature", ACSecurityHandler::FAIL);

  uint8_t pubsign_tmp[CURVE259919_KEYLEN];
  uint8_t pubencr_tmp[CURVE259919_SESSIONLEN];

  const bool tofu = (eeprom.flags & CRYPTO_HAS_MASTER_TOFU) ? true : false;

  // XX fix me - this is asking for trouble 4 state variables; 16 permutions
  // and only a few secure. Rewrite!
  uint8_t * signkey = NULL; // tentative signing key in case of tofu
  bool newsession = false;
  bool nonceOk = false;

  char * q = index(p, ' ');
  size_t cmd_len = _min(sizeof(req->cmd), (q && *q) ? q - p : strlen(p));
  req->cmd[cmd_len] = '\0';
  strncpy(req->cmd, p, cmd_len);

  p = q;
  while (p && *p == ' ') p++;

  if (strcmp(req->cmd, "welcome") == 0  || strcmp(req->cmd, "announce") == 0) {
    newsession = true;

    SEP(host_ip, "IP address", ACSecurityHandler::FAIL);
    // Debug.printf(" ** host_ip=%s\n", host_ip);
    SEP(master_publicsignkey_b64, "B64 public signing key", ACSecurityHandler::FAIL);
    // Debug.printf(" ** master_publicsignkey_b64=%s\n", master_publicsignkey_b64);
    SEP(master_publicencryptkey_b64, "B64 public encryption key", ACSecurityHandler::FAIL);
    // Debug.printf(" ** master_publicencryptkey_b64=%s\n", master_publicencryptkey_b64);

    B64DE(master_publicsignkey_b64, pubsign_tmp, "Ed25519 public key", ACSecurityHandler::FAIL);
    B64DE(master_publicencryptkey_b64, pubencr_tmp, "Curve25519 public key", ACSecurityHandler::FAIL);

    if (!bcmp(pubsign_tmp, node_publicsign, sizeof(node_publicsign))) {
      Debug.println("Ignoring - am hearing myself.");
      return ACSecurityHandler::OK;
    };
    if (strcmp(req->cmd, "welcome") == 0) {
      SEP(nonce, "Nonce extraction", ACSecurityHandler::FAIL);

      Debug.printf("Nonce %s = %s\n", _nonce, nonce);
      if (!strcmp(_nonce, nonce))
        nonceOk = true;
    };
    if (tofu) {
      if (memcmp(eeprom.master_publicsignkey, pubsign_tmp, sizeof(eeprom.master_publicsignkey))) {
        Log.println("Sender has changed its public signing key(s) - ignoring.");
        return ACSecurityHandler::FAIL;
      }
      Trace.println("Recognize the Ed25519 signature of the master on message from earlier TOFU.");
      signkey = eeprom.master_publicsignkey;
    } else {
      Debug.println("Unknown Ed25519 signature on message - giving the benefit of the doubt.");

      // We are not setting the TOFU flag in the EEPROM yet, as we've not yet checked
      // for reply by means of the beat.
      //
      signkey = pubsign_tmp;
    }
  };
  if (signkey == NULL && tofu) {
    Trace.println("Using existing master keys to verify.");
    signkey = eeprom.master_publicsignkey;
  }
  else if (signkey == NULL) {
    Log.println("Cannot (yet) validate signature - ignoring while waiting for welcome/announce");
    return ACSecurityHandler::FAIL;
  };

  resetWatchdog();
  if (!Ed25519::verify(signature, signkey, req->rest, strlen(req->rest))) {
    Log.println("Invalid Ed25519 signature on message -rejecting.");
    Debug.printf(" ** Payload is <%s>\n", req->rest);
    return ACSecurityHandler::FAIL;
  };

  beat_t delta = beat_absdelta(req->beatExtracted, beatCounter);
  if (nonceOk) {
    Debug.println("Verified nonce; so any beat ok.");
  }
  else if (delta < 120) {
    Trace.println("Beat ok.");
  }
  else if (strcmp(req->cmd, "announce") == 0) {
    Debug.printf("Beat too far off (%lu) - sending nonced welcome <%s>\n",
                 delta, _nonce);

    _acnode->send_helo();
    return ACSecurityHandler::FAIL;
  }
  else {
    Log.printf("Beat is too far off (%lu) - rejecting without a nonce\n", delta);
    // or should we send a noned welcome to get back on track ?
    return ACSecurityHandler::FAIL;
  };

  if (!tofu && nonceOk) {
    Log.println("TOFU for Ed25519 key of master, stored in persistent store.");
    memcpy(eeprom.master_publicsignkey, signkey, sizeof(eeprom.master_publicsignkey));

    eeprom.flags |= CRYPTO_HAS_MASTER_TOFU;
    save_eeprom();
  }
  else {
    Debug.println("Trusted; based on data from presistent store.");
  };
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

    encode_base64(pubsign_tmp, sizeof(pubsign_tmp), (unsigned char *)master_publicsignkey_b64);
    encode_base64(pubencr_tmp, sizeof(pubencr_tmp), (unsigned char *)master_publicencryptkey_b64);

    Log.printf("(Re)calculated session key - slaved to master public signkey %s and masterpublic encrypt key %s\n",
               master_publicsignkey_b64, master_publicencryptkey_b64);

    eeprom.flags |= CRYPTO_HAS_MASTER_TOFU;
  };

  // Debug.printf("==> Final %s\n", req->rest);

  return  Beat::verify(req);
};

SIG2::acauth_result_t SIG2::secure(ACRequest * req) {
  Serial.println("SIG2::secure");

  char msg[MAX_MSG];

  acauth_result_t r = Beat::secure(req);
  if (r == FAIL || r == OK)
    return r;

  if (!sig2_active())
    return FAIL;

  uint8_t signature[ED59919_SIGLEN];

  resetWatchdog();
  Ed25519::sign(signature, eeprom.node_privatesign, node_publicsign, req->payload, strlen(req->payload));

  char sigb64[ED59919_SIGLEN * 2]; // plenty for an HMAC and for a 64 byte signature.
  encode_base64(signature, sizeof(signature), (unsigned char *)sigb64);

  strncpy(req->version, "SIG/2.0", sizeof(req->version));
  snprintf(msg, MAX_MSG, "%s %s %s", req->version, sigb64, req->payload);

  strncpy(req->payload, msg, sizeof(req->payload));
  return OK;
};

SIG2::acauth_result_t SIG2::cloak(ACRequest * req) {
  char tag_encoded[MAX_MSG];

  if (!sig2_active())
    return ACSecurityHandler::FAIL;

  CBC<AES256> cipher;

  uint8_t iv[16];
  RNG.rand(iv, sizeof(iv));

  if (!cipher.setKey(sessionkey, cipher.keySize())) {
    Log.println("FAIL setKey");
    return FAIL;
  }

  if (!cipher.setIV(iv, cipher.ivSize())) {
    Log.println("FAIL setIV");
    return FAIL;
  }

  // PKCS#7 padding - as traditionally used with AES.
  // https://www.ietf.org/rfc/rfc2315.txt
  // -- section 10.3, page 21 Note 2.
  //
  size_t len = strlen(req->tag);
  int pad = 16 - (len % 16); // cipher.blockSize();
  if (pad == 0) pad = 16; //cipher.blockSize();

  size_t paddedlen = len + pad;
  uint8_t input[ paddedlen ], output[ paddedlen ], output_b64[ paddedlen * 4 / 3 + 4  ], iv_b64[ 32 ];
  strcpy((char *)input, req->tag);

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

  snprintf(tag_encoded, sizeof(tag_encoded), "%s.%s", iv_b64, output_b64);

  strncpy(req->tag, tag_encoded, sizeof(req->tag));

  return OK;
};

SIG2::cmd_result_t SIG2::handle_cmd(ACRequest * req)
{
  if (!strncmp("welcome", req->cmd, 7)) {
    return CMD_CLAIMED;
  }
  if (!strncmp("announce", req->cmd, 8)) {
    Debug.println("Sending from handlecmd in SIG2");

    /// I think we can drop this one.
    _acnode->send_helo();
    return CMD_CLAIMED;
  }
  return Beat::handle_cmd(req);
}

SIG2::acauth_result_t SIG2::helo(ACRequest * req) {
  char buff[MAX_MSG];
  if (!sig2_active()) {
    Debug.printf("Not sending %s from _send_helo() - not yet active.\n", req->payload);
    return FAIL;
  };

  IPAddress myIp = _acnode->localIP();
  snprintf(buff, sizeof(buff), "%s %d.%d.%d.%d", req->payload, myIp[0], myIp[1], myIp[2], myIp[3]);

  char b64[128];

  // Add ED25519 signing/non-repudiation key
  //
  strncat(buff, " ", sizeof(buff));
  encode_base64((unsigned char *)node_publicsign, sizeof(node_publicsign), (unsigned char *)b64);
  strncat(buff, b64, sizeof(buff));

  // Add Curve25519 session/confidentiality key
  //
  strncat(buff, " ", sizeof(buff));
  encode_base64((unsigned char *)(node_publicsession), sizeof(node_publicsession), (unsigned char *)b64);
  strncat(buff, b64, sizeof(buff));

  // Add a nonce - so we can time-point the reply.
  //
  uint8_t nonce_raw[ HASH_LENGTH ];
  RNG.rand(nonce_raw, sizeof(nonce_raw));
  SHA256 sha256;
  sha256.reset();
  sha256.update(node_publicsign, sizeof(node_publicsign));
  sha256.update(node_publicsession, sizeof(node_publicsession));
  sha256.update(nonce_raw, sizeof(nonce_raw));
  sha256.finalize(nonce_raw, sizeof(nonce_raw));

  // We know that this fits - see header of SIG2.h
  //
  encode_base64(nonce_raw, sizeof(nonce_raw),  (unsigned char *)_nonce);

  // Append the noce.
  strncat(buff, " ", sizeof(buff));
  strncat(buff, _nonce, sizeof(buff));

  strncpy(req->payload, buff, sizeof(req->payload));
  return OK;
}

