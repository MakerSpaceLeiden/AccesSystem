#pragma once

#include "MakerSpaceMQTT.h" // needed for MAX_MSG

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

#define RNG_APP_TAG BUILD
#define RNG_EEPROM_ADDRESS (sizeof(eeprom)+4)

#define SIG2

extern bool sig2_active();
extern void kickoff_RNG();
extern void maintain_rng();
extern void load_eeprom();
extern void save_eeprom();
extern void wipe_eeprom();
extern void init_curve();
extern int setup_curve25519();

// Option 1 (caninical) - a (correctly) signed message with a known key.
// Option 2 - a (correctly) signed message with an unknwon key and still pre-TOFU
//            we need to check against the key passed rather than the one we know.
// Option 3 - a (correctly) signed message with an unknwon key - which is not the same as the TOFU key
// Option 4 - a (incorrectly) signed message. Regardless of TOFU state.

extern bool sig2_verify(const char * beat, const char signature64[], const char signed_payload[]);
extern  void sig2_sign(char msg[MAX_MSG], size_t maxlen, const char * tosign);
extern const char * sig2_encrypt(const char * lasttag, char * tag_encoded, size_t maxlen);


