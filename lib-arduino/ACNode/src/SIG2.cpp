#include <ACNode-private.h>
#include "SIG2.h"
#include "MakerSpaceMQTT.h" // needed for MAX_MSG
#include <Arduino.h> // min() macro
#include <Crypto.h>
#include <Curve25519.h>
#include <Ed25519.h>
#include <RNG.h>
#include <SHA256.h>

#include <EEPROM.h>
#include <AES.h>
#include <CBC.h>

#include <unordered_map>

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

#define EEPROM_PRIVATE_OFFSET (0x100)
#define EEPROM_RND_OFFSET (EEPROM_PRIVATE_OFFSET + sizeof(eeprom_t))

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

// Keys upon whcih trust can be registed. the requested field is used for a nonce; but can be used as an age.
typedef struct { char node[MAX_NAME]; uint8_t pubkey[CURVE259919_KEYLEN]; unsigned long requested; } trust_t;
#define MAX_TRUST_N (8)
trust_t trust[ MAX_TRUST_N ];
int nTrusted = 0;

static int init_done = 0;

bool sig2_active() {
  return (eeprom.flags & CRYPTO_HAS_PRIVATE_KEYS && init_done >= 2);
}

uint8_t runtime_seed[ 32 ];

void kickoff_RNG() {
  // Attempt to get a half decent seed soon after boot. We ought to pospone all operations
  // to the run loop - well after DHCP has gotten is into business.
  //
  // Note that Wifi/BT should be on according to:
  //    https://github.com/espressif/esp-idf/blob/master/components/esp32/hw_random.c
  //
  RNG.begin(RNG_APP_TAG);

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

  uint8_t result[sha256.hashSize()];
  sha256.finalize(result, sizeof(result));
  RNG.stir(result, sizeof(result), 100);

  RNG.rand(runtime_seed,sizeof(runtime_seed));
  
  RNG.setAutoSaveTime(60);
}
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
    Serial.printf("EEPROM Version %04x not understood -- clearing.\n", eeprom.version );
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

  if (!_acnode->isConnected()) {
    // force re-connecting, etc post reconnect.
    if (init_done > 4) init_done = 4;
    return;
  };

  if (init_done > 4)
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
  // transition from 3->4 is once we are slaved.
  // 
  if (init_done == 4) {
    for(int i = 0; i < nTrusted; i++)
	request_trust(i);
    init_done  = 5;
    return;
  };
} 

// Note - we're not siging the topic.
//
ACSecurityHandler::acauth_result_t SIG2::verify(ACRequest * req) {
  size_t len = strlen(req->payload);

  char * sender = rindex(req->topic,'/');
  if (!sender) {
	Log.println("No sender in topic. giving up.");
	return ACSecurityHandler::FAIL;
  };
  sender++; // Skip '/'.
  bool sendIsMaster = !strcmp(_acnode->master, sender);

  // We only accept things starting with SIG/2*<space>hex<space>
  if (len < 72 || strncmp(req->payload, "SIG/2.", 6) != 0) 
    return ACSecurityHandler::DECLINE;

  if (len > sizeof(req->tmp) - 1) {
    Debug.println("Failing SIG/2 sigature - far too long");
    return FAIL;
  };

  strncpy(req->tmp, req->payload, sizeof(req->tmp));
  char * p = req->tmp;

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
  if (abs(((int)strlen(signature64)) - ((int)B64L(ED59919_SIGLEN))) > 3) {
    Debug.printf("Failing SIG/2 sigature - wrong length signature64 (%d,%d)\n",
                 strlen(signature64), B64L(ED59919_SIGLEN));
    return ACSecurityHandler::FAIL;
  };

  req->beatExtracted = strtoul(req->beat, NULL, 10);
  if (req->beatExtracted == 0) {
    Debug.println("Failing SIG/2 sigature - beat parsing failed");
    return ACSecurityHandler::FAIL;
  };

  // Option 1 (caninical) - a (correctly) signed message with a known key; signed by the master.
  // Option 2 - a (correctly) signed message with an unknwon key and still pre-TOFU
  //            we need to check against the key passed rather than the one we know.
  // Option 3 - a (correctly) signed message with an unknwon key - which is not the same as the TOFU key
  // Option 3 -- as above; but not coming from the master but from a node we trust and have the pubkey of.
  // Option 4 - a (incorrectly) signed message. Regardless of TOFU state.
  //
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

  if (sendIsMaster && (strcmp(req->cmd, "welcome") == 0  || strcmp(req->cmd, "announce") == 0)) {
    newsession = true;

    SEP(host_ip, "IP address", ACSecurityHandler::FAIL);
    SEP(master_publicsignkey_b64, "B64 public signing key", ACSecurityHandler::FAIL);
    SEP(master_publicencryptkey_b64, "B64 public encryption key", ACSecurityHandler::FAIL);

    B64DE(master_publicsignkey_b64, pubsign_tmp, "Ed25519 public key", ACSecurityHandler::FAIL);
    B64DE(master_publicencryptkey_b64, pubencr_tmp, "Curve25519 public key", ACSecurityHandler::FAIL);

    if (!bcmp(pubsign_tmp, node_publicsign, sizeof(node_publicsign))) {
      Debug.println("Ignoring - am hearing myself.");
      return ACSecurityHandler::OK;
    };
    if (strcmp(req->cmd, "welcome") == 0) {
      SEP(nonce, "Nonce extraction", ACSecurityHandler::FAIL);
  
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
  } else 
  if (!sendIsMaster && nTrusted) {
    // needs to be re-written - too many easy to miss code paths that may have vulnerabilities/bypasses.
    for(int i = 0; i < nTrusted; i++) 
	if (strcmp(sender, trust[i].node) == 0) {
		signkey = trust[i].pubkey;
		Debug.printf("Allowing this message to be signed by node %s\n", sender);
		break;
	};
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
    return ACSecurityHandler::FAIL;
  };

  beat_t delta = beat_absdelta(req->beatExtracted, beatCounter);
  if (nonceOk) {
    Debug.println("Verified nonce; so any beat ok.");
  }
  else if (delta < 1200) { // Apparently we can drive minutes in a short space of time.
    Trace.println("Beat ok.");
  }
  else if (strcmp(req->cmd, "announce") == 0) {
    Debug.printf("Beat too far off (%lu) - sending nonced welcome\n", delta);
    _acnode->send_helo();
    return ACSecurityHandler::FAIL;
  }
  else {
    Log.printf("Beat is too far off (%lu) - rejecting without a nonce\n", delta);
    // or should we send a noned welcome to get back on track ?
    return ACSecurityHandler::FAIL;
  };

  if (!tofu && nonceOk && sendIsMaster) {
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

    encode_base64(pubsign_tmp, sizeof(pubsign_tmp), (unsigned char *)master_publicsignkey_b64);
    encode_base64(pubencr_tmp, sizeof(pubencr_tmp), (unsigned char *)master_publicencryptkey_b64);

    Log.printf("(Re)calculated session key - slaved to master public signkey %s and masterpublic encrypt key %s\n",
               master_publicsignkey_b64, master_publicencryptkey_b64);

    // Only accept pubkeys on poweron; not on simple server restarts.
    // So that a temp-fault/hack at the server does not mean wrong
    // keys in all nodes.
    //
    if (init_done < 4)
        init_done = 4;

    eeprom.flags |= CRYPTO_HAS_MASTER_TOFU;
  };

  return  Beat::verify(req);
};

SIG2::acauth_result_t SIG2::secure(ACRequest * req) {

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
  snprintf(req->tmp, sizeof(req->payload), "%s %s %s", req->version, sigb64, req->payload);
  strncpy(req->payload, req->tmp, sizeof(req->payload));

  return OK;
};

SIG2::acauth_result_t SIG2::cloak(ACRequest * req) {
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

  snprintf(req->tag, sizeof(req->tag), "%s.%s", iv_b64, output_b64);
  return OK;
};

SIG2::cmd_result_t SIG2::handle_cmd(ACRequest * req)
{
  if (!strncmp("welcome", req->cmd, 7)) {
    return CMD_CLAIMED;
  }
  if (!strncmp("announce", req->cmd, 8)) {
    /// I think we can drop this one.
    _acnode->send_helo();
    return CMD_CLAIMED;
  }
  if (!strncmp("trust", req->cmd, 5)) {
	strncpy(req->tmp, req->rest, sizeof(req->tmp));
	char * p = req->tmp;

        SEP(nonce , "Trust failed - no nonce", CMD_CLAIMED);
        SEP(node, "Trust failed - no node name", CMD_CLAIMED);
        SEP(b64pubkey, "Trust failed - no pubkey", CMD_CLAIMED);

        if (strlen(node) >= MAX_NAME) {
        	Log.println("Trust failed - invalid node name");
		return CMD_CLAIMED;
	};

        if (strlen(b64pubkey) >= B64L(CURVE259919_KEYLEN)) {
        	Log.println( "Trust failed - invalid pub key");
		return CMD_CLAIMED;
	};

        // Is this a node name we are interested in ?
	//
        int i  = 0;
        for(i = 0; i < nTrusted; i++) if (!strcmp(node, trust[ i ].node)) break;
	if (i == nTrusted) {
		Log.println("Trust failed - not a node we'er interested in.");
		return CMD_CLAIMED;
	};

        char mynonce[ B64L(HASH_LENGTH) ];
        char seed[32];
        snprintf(seed,sizeof(seed),"%lu-%s",trust[i].requested,trust[i].node);
        populate_nonce(seed, mynonce); 

        if (strcmp(nonce,mynonce)) {
		Log.println("Trust failed - not my (last) nonce");
		return CMD_CLAIMED;
	}

        B64DE(b64pubkey, trust[ i].pubkey, "Trust failed - key decode", CMD_CLAIMED);

        Log.printf("Got trust in <%s> - public key stored.\n", node);

	// Update the timer - so block some sort of reply.
	//
        trust[i].requested = 1+millis();

	return CMD_CLAIMED;
  }
  return Beat::handle_cmd(req);
}

void SIG2::add_trusted_node(const char *node) {
        if (nTrusted >= MAX_TRUST_N) {
        	Log.println("Hit hardcoded limit on number of nodes I can trust.");
		return;
        };
        if(strlen(node) >= sizeof(trust[ nTrusted ].node)) {
		Log.println("Name to trust too long. Ignoring.");
		return;
	};
        strncpy(trust[ nTrusted ].node, node, sizeof(trust[ nTrusted ].node));
	bzero(trust[nTrusted].pubkey,sizeof(trust[nTrusted].pubkey));

        if (init_done > 4)
		request_trust(nTrusted);

	nTrusted++;
}

void SIG2::request_trust(int i) {
	trust[i].requested = millis();

        char mynonce[ B64L(HASH_LENGTH) ];
        char seed[32];
        snprintf(seed,sizeof(seed),"%lu-%s",trust[i].requested,trust[i].node);
        populate_nonce(seed, mynonce); 

        char payload[ 6 + 1 + B64L(HASH_LENGTH) + 1 + MAX_NAME + 1];
	snprintf(payload, sizeof(payload), "pubkey %s %s", mynonce, trust[i].node);

	Debug.printf("Requesting trust for <%s>\n", trust[i].node);
    	_acnode->send(payload);

        char topic[MAX_TOPIC];
        snprintf(topic, sizeof(topic), "%s/%s/%s", 
		_acnode->mqtt_topic_prefix, _acnode->moi, trust[i].node);
        _acnode->_client.subscribe(topic);

	Debug.printf("Subscribing to %s for the trusted messages.>\n", topic);
};

SIG2::acauth_result_t SIG2::helo(ACRequest * req) {
  if (!sig2_active()) {
    Debug.printf("Not sending %s from _send_helo() - not yet active.\n", req->payload);
    return ACSecurityHandler::DECLINE;
  };

  IPAddress myIp = _acnode->localIP();
  char buff[MAX_TOKEN_LEN * 2];
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
  populate_nonce(NULL,_nonce);

  strncat(buff, " ", sizeof(buff));
  strncat(buff, _nonce, sizeof(buff));

  strncpy(req->payload, buff, sizeof(req->payload));
  return OK;
}

void SIG2::populate_nonce(const char * seedOrNull, char nonce[B64L(HASH_LENGTH)]) {
  uint8_t nonce_raw[ HASH_LENGTH ];
  SHA256 sha256;
  sha256.reset();
  sha256.update(runtime_seed, sizeof(runtime_seed));
  sha256.update(node_publicsign, sizeof(node_publicsign));
  sha256.update(node_publicsession, sizeof(node_publicsession));
  if (seedOrNull)
	sha256.update(seedOrNull, strlen(seedOrNull));
  else {
	RNG.rand(nonce_raw, sizeof(nonce_raw));
  	sha256.update(nonce_raw, sizeof(nonce_raw));
  };
  sha256.finalize(nonce_raw, sizeof(nonce_raw));

  // We know that this fits - see header of SIG2.h
  //
  encode_base64(nonce_raw, sizeof(nonce_raw),  (unsigned char *)nonce);
};

