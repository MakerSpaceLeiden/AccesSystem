import os, sys
from asn1crypto import keys, core, pem
from asn1crypto.core import ( Integer, ObjectIdentifier, OctetString, Any )

'''
    0:d=0  hl=2 l=  46 cons: SEQUENCE          
    2:d=1  hl=2 l=   1 prim:  INTEGER           :00
    5:d=1  hl=2 l=   5 cons:  SEQUENCE          
    7:d=2  hl=2 l=   3 prim:   OBJECT            :1.3.101.112
   12:d=1  hl=2 l=  34 prim:  OCTET STRING      [HEX DUMP]:0420D4EE72DBF913584AD5B6D8F1F769F8AD3AFE7C28CBF1D4FBE097A88F44755842
'''

# draft-ietf-curdle-pkix-01, RFC 5912
#
'''
3.  Curve25519 and Curve448 Named Curve Identifier

   Certificates conforming to [RFC5280] may convey a public key for any
   public key algorithm.  The certificate indicates the algorithm
   through an algorithm identifier.  This algorithm identifier is an OID
   and optionally associated parameters.  Section 2.3.5 of [RFC3279]
   describe ECDSA/ECDH public keys, specifying the id-ecPublicKey OID.
   This OID has the associated EcpkParameters parameters structure,
   which contains the namedCurve CHOICE.  Here we introduce two new OIDs
   for use in the namedCurve field.

       id-Curve25519   OBJECT IDENTIFIER ::= { 1.3.101.110 }
       id-Curve448     OBJECT IDENTIFIER ::= { 1.3.101.111 }
       id-Curve25519ph OBJECT IDENTIFIER ::= { 1.3.101.112 }
       id-Curve448ph   OBJECT IDENTIFIER ::= { 1.3.101.113 }

   The OID id-Curve25519 refers to Curve25519.  The OID id-Curve448
   refers to Curve448.  Both curves are described in [RFC7748].  The
   OIDs id-Curve25519ph and id-Curve448ph refers to Curve25519 and
   Curve448 when used with pre-hashing as Ed25519ph and Ed448ph
   described in [I-D.irtf-cfrg-eddsa].
'''
class AlgorithmIdentifier(ObjectIdentifier):
        _map = {
           '1.3.101.110': 'Curve25519',
           '1.3.101.112': 'Ed25519',
        }

class PrivateKeyAlgorithmIdentifier(keys.Sequence):
        _fields = [
            ('algorithm', AlgorithmIdentifier),
            ('parameters', Any, {'optional': True}),
        ]

class PrivateKey(OctetString):
        _fields = [
            ('key', OctetString),
        ]

class OneAsymmetricKey(keys.Sequence):
        _fields = [
            ('version', Integer),
            ('privateKeyAlgorithm', PrivateKeyAlgorithmIdentifier),
            ('privateKey', OctetString),
        ]

class Curve25519:

    def decode(byte_string):
      try:
        if pem.detect(byte_string): 
            type_name, headers, decoded_bytes = pem.unarmor(byte_string)
            byte_string = decoded_bytes

        key = OneAsymmetricKey.load(byte_string)
        if key['privateKeyAlgorithm']['algorithm'].native != 'Curve25519' and key['privateKeyAlgorithm']['algorithm'].native != 'Ed25519':
            raise NotACurve25519Key
        
        key = PrivateKey.load(key['privateKey'].native)

        if len(key.native) != 32:
            raise NotA256BitCurve25519Key
      
      except Exception as e:
            raise FailedToParseCurve25519Key
      
      return key.native

    def encode(byte_string, is_der = None, alg = 'Curve25519' ):
        try:
            raw = OctetString(byte_string)
        except Exception as e:
            raise FailedToParseByteString
        
        privateKey = PrivateKey(raw.dump())
        
        privateKeyAlgorithmIdentifier = PrivateKeyAlgorithmIdentifier( {'algorithm': alg });
        
        oneAsymmetricKey = OneAsymmetricKey({
            'version': Integer(0), 
            'privateKeyAlgorithm': privateKeyAlgorithmIdentifier,
            'privateKey': privateKey})
            
        der = oneAsymmetricKey.dump()

        if is_der:
             return der 

        return pem.armor('PRIVATE KEY',der).decode('ASCII')

if __name__ == "__main__":
  testVectors = {
	'38803cb388a178d029d958ec8b246626283a2ce6570e47ca19d76b370d684167': "-----BEGIN PRIVATE KEY-----\nMC4CAQAwBQYDK2VuBCIEIDiAPLOIoXjQKdlY7IskZiYoOizmVw5HyhnXazcNaEFn\n-----END PRIVATE KEY-----\n",
	'd4ee72dbf913584ad5b6d8f1f769f8ad3afe7c28cbf1d4fbe097a88f44755842': "-----BEGIN PRIVATE KEY-----\nMC4CAQAwBQYDK2VwBCIEINTuctv5E1hK1bbY8fdp+K06/nwoy/HU++CXqI9EdVhC\n-----END PRIVATE KEY-----\n"
  }
  for h,p in testVectors.items():
     key = Curve25519.decode(p.encode('ASCII'))
     if h != key.hex():
        raise fail
     # print(key.hex())
     bstring = bytes.fromhex(h)
     out = Curve25519.encode(bstring)
     print(out)

  print("Ok")


