/* Test scripts that generates two key pairs using the Arduino Curve25519 script;
 * and then outputs a python script to verify that one gets the same, interoperable,
 * results on the unix side of things.
 *
 * Part of the Makerspace Leiden Access system -- see the README and LICENSE file in the main
 * directory for more information.
 */
 
#include <Crypto.h>
#include <Curve25519.h>
#include <string.h>
#include <base64.hpp>
#include <assert.h>
#include <Ed25519.h>
#include <SHA256.h>
#include <RNG.h>
#include <AES.h>
#include <CTR.h>

#define RNG_APP_TAG __FILE__ __DATE__ __TIME__
#define RNG_EEPROM_ADDRESS 950

void b64print(const char * tag, uint8_t * bytes, size_t len) {
  Serial.print(tag);
  Serial.print("=base64.b64decode(\"");

  unsigned char tmp[1024];
  encode_base64(bytes, len, tmp);
  Serial.print((char *)tmp);

  Serial.print("\")   # Len ");
  Serial.println(len);
}

void kickoff_RNG() {
  // Attempt to get a half decent seed soon after boot. We ought to pospone all operations
  // to the run loop - well after DHCP has gotten is into business.
  //
  // Note that Wifi/BT should be on according to:
  //    https://github.com/espressif/esp-idf/blob/master/components/esp32/hw_random.c
  //
  RNG.begin(RNG_APP_TAG, RNG_EEPROM_ADDRESS);

  SHA256 Sha256;
  uint8_t seed[ 32 ];
  for (int i = 0; i < 25; i++) {
    uint32_t r = RANDOM_REG32 ; // Or esp_random(); for the ESP32 in recent libraries.
    Sha256.update(&r, sizeof(r));
    delay(10);
  }
  Sha256.finalize(seed, sizeof(seed));
  RNG.stir(seed, 32, 100);
}

void setup() {
  uint8_t shared_key[32], tmp_private[32], key[32];
  uint8_t master_public[32];
  uint8_t master_private[32];

  uint8_t node_public[32];
  uint8_t node_private[32];

  Serial.begin(9600);
  Serial.println("\n\n\nFile: " __FILE__ " Compiled " __DATE__ " " __TIME__ "\n\n");
  kickoff_RNG();

  // Calcuate two key pairs. One for the master, one for the node.
  //
  Curve25519::dh1(master_public, master_private);
  Curve25519::dh1(node_public, node_private);

  // dh2 'destroys' its inputs; replacing the public key
  // by the shared key; and nulling hte private key. So
  // we copy it to a safe place prior to the calculation.

  memcpy(shared_key, master_public, sizeof(shared_key));
  memcpy(tmp_private, node_private, sizeof(node_private));

  // Generate the shared key to use to send information to the master:
  // For the real code we are to use separate session and signing keys.

  Curve25519::dh2(shared_key, tmp_private);

  // Hash this - as suggested in the paper of Curve 25519 (Cargo Culting!)
  SHA256 Sha256;
  Sha256.reset();
  Sha256.update(shared_key, sizeof(shared_key));
  Sha256.finalize(key, sizeof(key));

  // Now output the results of this as a python script which we can run - and which should give us the
  // same results
  Serial.println("#!python");
  Serial.println("# Test script to validate Curve25519 interoperability between");
  Serial.println("# Arduino and two python libraries.\n");

  Serial.println("import axolotl_curve25519 as curve");
  Serial.println("from donna25519 import PrivateKey");
  Serial.println("from donna25519 import PublicKey");
  Serial.println("import hashlib");
  Serial.println("import base64\n");

  b64print("master_public", master_public, sizeof(master_public));
  b64print("master_private", master_private, sizeof(master_private));

  b64print("node_private", node_private, sizeof(node_private));
  b64print("node_public ", node_public, sizeof(node_public));
  b64print("shared_key  ", shared_key, sizeof(shared_key));
  b64print("hashed_key  ", key, sizeof(key));

  Serial.println("print \"Key \" + base64.b64encode(shared_key)");
  Serial.println("pub = PublicKey(master_public)");
  Serial.println("priv = PrivateKey(node_private)");

  Serial.println("shared = priv.do_exchange(pub)");
  Serial.println("print \"Key \" + base64.b64encode(shared)");

  Serial.println("session_key = curve.calculateAgreement(node_private, master_public)");
  Serial.println("print \"Key \" + base64.b64encode(session_key)");

  Serial.println("pub = PublicKey(node_public)");
  Serial.println("priv = PrivateKey(master_private)");

  Serial.println("shared = priv.do_exchange(pub)");
  Serial.println("print \"Key \" + base64.b64encode(shared)");

  Serial.println("session_key = curve.calculateAgreement(master_private, node_public)");
  Serial.println("print \"Key \" + base64.b64encode(session_key)");

  Serial.println("hashed = hashlib.sha256(session_key).digest()");
  Serial.println("print \"Hash \" + base64.b64encode(hashed_key)");
  Serial.println("print \"Hash \" + base64.b64encode(hashed)");

  return;
}

void loop() {
  // put your main code here, to run repeatedly:

}
