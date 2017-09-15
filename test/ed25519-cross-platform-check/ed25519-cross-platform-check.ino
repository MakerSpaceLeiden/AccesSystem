/* Test scripts that checks interoperability of a ED25519 signature across unix
 * and arduino.
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

extern uint32_t esp_random(void);

/* --
    import ed25519
    private_key, public_key = ed25519.create_keypair()
    message = b"Ok to open that door."

    print private_key.to_ascii(encoding="base64")
    print public_key.to_ascii(encoding="base64")
    print private_key.sign(message, encoding="base64")
*/

/* OUTPUT:
  +NuGVz0S6cPkj9I5Qh6ALi1XVORQDo1k6h1zeosF9Pk
  udKeLsYgEmYeZalE8cXzodX0Ww4h7i1UCy30hM+oQNQ
  aEuMhbKGsPpI+4dHK4you0gJsR26WZZZGNoCmGCYGQSGNFszk9+01MJc3iARNErw9ChiBSldqcaZ4gUSQN8JBw
*/
// Verify something I receive.
//
const char message_in[] =           "Ok to open that door.";
unsigned char master_private_b64[] = "+NuGVz0S6cPkj9I5Qh6ALi1XVORQDo1k6h1zeosF9Pk="; // needed for the example at the end.
unsigned char master_public_b64[] =  "udKeLsYgEmYeZalE8cXzodX0Ww4h7i1UCy30hM+oQNQ=";
unsigned char signature_b64[] =     "aEuMhbKGsPpI+4dHK4you0gJsR26WZZZGNoCmGCYGQSGNFszk9+01MJc3iARNErw9ChiBSldqcaZ4gUSQN8JBw";

void b64print(const char * tag, uint8_t * bytes, size_t len) {
  Serial.print(tag);
  Serial.print(" (");
  Serial.print(len);
  Serial.print(")\t:");

  unsigned char tmp[1024];
  encode_base64(bytes, len, tmp);

  Serial.println((char *)tmp);
}

void setup() {
  Serial.begin(9600);
  Serial.println("\n\n\nFile: " __FILE__ " Compiled " __DATE__ " " __TIME__ "\n\n");

  unsigned char master_public[ 32 ];
  unsigned char signature[ 64 ];
  size_t len;

  len = decode_base64(master_public_b64, master_public);
  Serial.printf("Pub key len %d\n", len);
  assert(len == sizeof(master_public));

  len = decode_base64(signature_b64, signature);
  Serial.printf("Sig len %d\n", len);
  assert(len == sizeof(signature));

  Serial.printf("Verification: %s\n", Ed25519::verify(signature, master_public,  message_in, strlen(message_in)) ? "ok" : "FAIL WHALE!");

  // Attempt to get a half decent seed soon after boot.
  //
  // Note that Wifi/BT should be on according to:
  //    https://github.com/espressif/esp-idf/blob/master/components/esp32/hw_random.c
  //
  RNG.begin(RNG_APP_TAG, RNG_EEPROM_ADDRESS);

  SHA256 Sha256;
  uint8_t seed[ 32 ];
  for (int i = 0; i < 100; i++) {
    uint32_t r = RANDOM_REG32 ; // Or esp_random(); for the ESP32 in recent libraries.
    Sha256.update(&r, sizeof(r));
  }
  Sha256.finalize(seed, sizeof(seed));
  RNG.stir(seed, 32, 100);

  // Generate something signed to check.
  //
  const char message_out[] = "Tag 1-2-3-4-5 - can I haz open door ?";

  uint8_t node_private[32];
  uint8_t node_public[32];
#if 0
  Ed25519::generatePrivateKey(node_private);
#else
  decode_base64((unsigned char *)"wTBrApLE1uAoFMfkJvHAFqwxqPX0g4//dVpg8kFS5j4=", node_private);
#endif
  Ed25519::derivePublicKey(node_public, node_private);

  Ed25519::sign(signature, node_private, node_public, message_out, strlen(message_out));

  unsigned char node_public_b64[ 128 ];
  encode_base64(node_public, sizeof(node_public), node_public_b64);
  encode_base64(signature, sizeof(signature), signature_b64);

  Serial.printf(
    "\n\nPython code to run to validate output\n\n"
    "import ed25519\n"
    "message = b\"%s\"\n"
    "pub = \"%s\"\n"
    "sig = \"%s\"\n"
    "\n"
    "try:\n"
    "  verifying_key = ed25519.VerifyingKey(pub, encoding=\"base64\")\n"
    "  verifying_key.verify(sig, message, encoding = \"base64\")\n"
    "  print \"All seems well.\"\n"
    "except ed25519.BadSignatureError:\n"
    "  print \"signature is bad!\"\n"
    "except: \n"
    "  print \"Some error.\"\n", message_out, node_public_b64, signature_b64);
}

void loop() {
  // put your main code here, to run repeatedly:

}
