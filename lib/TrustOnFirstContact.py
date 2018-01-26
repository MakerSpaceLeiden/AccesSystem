#!/usr/bin/env python3.4
#

import time
import sys
import os
import base64 
import socket
import traceback
import hashlib
import linecache

import axolotl_curve25519 as curve
import ed25519 as ed25519
from Crypto.Cipher import AES

# For older version of python use:
# from Cryptodome import Random
from Crypto import Random
import SharedSecret
import Beat

# Note - not checking for illegal padding.
pkcs7_unpad = lambda s : s[0:-ord(s[-1])]

class TrustOnFirstContact(Beat.Beat):
  sharedkey = {}
  pubkeys = {}
  
  
  def __init__(self):
    super().__init__()
    self.commands[ 'welcome' ] = self.cmd_welcome

  def setup(self):
    super().setup()

    if self.cnf.newprivatekey:
       if self.cnf.privatekey:
              self.logger.critical("Either generate a new key; or set one. Not both. Aborting.")
              sys.exit(1);
              
       privatekey, publickey = ed25519.create_keypair()

       if self.cnf.privatekeyfile:
           if os.path.isfile(self.cnf.privatekeyfile):
              self.logger.critical("Refusing to overwrite existing private key. Aborting.")
              sys.exit(1);

           if 32 != open(self.cnf.privatekeyfile,"wb").write(privatekey.to_seed()):
              self.logger.critical("Failed to write the newly generated privatekeyfile. Aborting.");
              sys.exit(1);

           self.logger.info("Wrote out newly generated private key");
       else:
           self.logger.info("Using empheral private key");

    if self.cnf.privatekey:
         seed = base64.b64decode(self.cnf.privatekey)

         if len(seed) != 32:
             self.logger.critical("Command line private key not exactly 32 bytes. aborting.");
             sys.exit(1);
             self.cnf.privatekey = ed25519.SigningKey(seed)
    else:
        if self.cnf.privatekeyfile:
             seed = open(self.cnf.privatekeyfile,"rb").read(32)

             if len(seed) != 32:
                 self.logger.critical("Private key in file is not exactly 32 bytes. aborting.");
                 sys.exit(1);
        else:
            self.logger.critical("No seed.");
            sys.exit(1);
        
    self.cnf.privatekey = ed25519.SigningKey(seed)

    if self.cnf.tofconly and not self.cnf.privatekey:
         self.logger.critical("No private key - cannot do TOFC . Aborting.")
         sys.exit(1);

    self.cnf.publickey = self.cnf.privatekey.get_verifying_key()
    self.pubkeys[ self.cnf.node ] = self.cnf.publickey

    self.session_priv = curve.generatePrivateKey(Random.new().read(32));
    self.session_pub =  curve.generatePublicKey(self.session_priv);
    
  def parseArguments(self):
    self.parser.add('--privatekey','-K',
         help='Private Curve25519 key (Default: unset, auto generated')
    self.parser.add('--privatekeyfile','-f',
         help='Private Curve25519 key file (Default: unset, auto generated')
    self.parser.add('--newprivatekey','-N',action='count',
         help='Generate a new private key, safe to privatekeyfile if defined')
    self.parser.add('--tofconly','-F',action='count',
         help='Force trust-on-first-use mode (default: off)')

    super().parseArguments()

  def send(self, dstnode, payload):
     if not self.cnf.privatekey:
        return super().send(dstnode, payload)

     payload = self.beat()+" "+payload
     signature = self.cnf.privatekey.sign(payload.encode('ASCII'))
     signature = base64.b64encode(signature).decode('ASCII')

     payload =  "SIG/2.0 "+signature+" "+payload
     
     super().send(dstnode, payload, raw=True)

  def extract_validated_payload(self, msg):
   try:
    if not msg['payload'].startswith("SIG/2"):
        return super().extract_validated_payload(msg)

    beat = int(self.beat())
    try:
        hdr, b64sig, payload = msg['payload'].split(' ',2)
        sig = base64.b64decode(b64sig)
        
        msg['hdr'] = hdr
        msg['sig'] = sig
        msg['payload'] = payload

        signed_payload = msg['payload']
        
        if not super().extract_validated_payload(msg):
           return None
        
        clean_payload = msg['payload']

        cmd = msg['payload'].split(" ")[0]
    except Exception as e:
        self.logger.warning("Could not parse curve25519 signature from payload '{}' -- ignored {}".format(msg['payload'],str(e)))
        return None

    if len(sig) != 64:
       self.logger.error("Signature wrong length for '{}' - ignored".format(msg['node']))
       return None

    # Process TOFU information if we do not (yet) know the public key
    # without validating the signature.
    #
    seskey = None
    if cmd == 'announce' or cmd == 'welcome':
            try:
                cmd, ip, pubkey, seskey  = clean_payload.split(" ")

            except Exception as e:
                self.logger.error("Error parsing announce. Ignored. "+str(e))
                return None

            publickey = base64.b64decode(pubkey)
            if len(publickey) != 32:
                    self.logger.error("Ignoring malformed signing public key of node {}".format(msg['node']))
                    return None

            publickey = ed25519.VerifyingKey(pubkey, encoding="base64")
            
            seskey = base64.b64decode(seskey)
            if len(seskey) != 32:
                    self.logger.error("Ignoring malformed session public key of node {}".format(msg['node']))
                    return None

            if msg['node'] in self.pubkeys:
                if self.pubkeys[msg['node']] != publickey:
                   self.logger.info ("Ignoring (changed) public key of node {}".format(msg['node']))
                   return None
            else:
                 self.logger.debug("Potentially learned a public key of node {} on first contact - checking signature next.".format(msg['node']))
    else:
        if not msg['node'] in self.pubkeys:
            self.logger.info ("No public key for node {} -- ignoring.".format(msg['node']))
            return None
        publickey = self.pubkeys[ msg['node'] ]
    
    if not self.cnf.privatekey:
        return None

    # Note: this is the payload `as of now' -- not the further decoded/stripped of its beat payload.
    # Because we also (want to) have the signature cover the beat - to prevent replay.
    # Todo: consider a nonce.
    try:
        publickey.verify(b64sig, signed_payload.encode('ASCII'), encoding="base64")
        self.logger.debug("Good signature on " + signed_payload)
    except ed25519.BadSignatureError:
        self.logger.warning("Bad signature for {} - ignored".format(msg['node']))
        return None
    except Exception as e:
        self.logger.warning("Invalid signature for {}: {} -- ignored.".format(msg['node'], str(e)))
        return None

    if not msg['node'] in self.pubkeys:
        self.pubkeys[ msg['node'] ] = publickey
        self.logger.info("Learned a public key of node {} on first contact.".format(msg['node']))

    if (cmd == 'announce' or cmd == 'welcome') and seskey:
        session_key = curve.calculateAgreement(self.session_priv, seskey)
        self.sharedkey[ msg['node'] ] = hashlib.sha256(session_key).digest()

        self.logger.debug("(Re)calculated shared secret with node {}.".format(msg['node']))

    msg['validated'] = 20
  
   except Exception as e:
        if 1:
            exc_type, exc_obj, tb = sys.exc_info()
            f = tb.tb_frame
            lineno = tb.tb_lineno
            filename = f.f_code.co_filename
            linecache.checkcache(filename)
            line = linecache.getline(filename, lineno, f.f_globals)
            self.logger.debug('EXCEPTION IN ({}, LINE {} "{}"): {}'.format(filename, lineno, line.strip(), exc_obj))
   return msg

  def send_tofu(self, prefix, dstnode):
    bp = self.cnf.publickey.to_bytes();
    bp = base64.b64encode(bp).decode('ASCII')
    
    sp = base64.b64encode(self.session_pub).decode('ASCII')
    
    return self.send(dstnode, prefix + " " + socket.gethostbyname(socket.gethostname()) + " " + bp + " " + sp)


  def session_decrypt(self, msg, cyphertext):
    node = msg[ 'node']
    
    if not node in self.sharedkey:
        self.logger.info("No session key for node {} - not decrypting.".format(node))
        return;
    
    try:
        iv, cyphertext = cyphertext.split('.')
        
        iv = base64.b64decode(iv)
        cyphertext = base64.b64decode(cyphertext)
        
        if len(iv) != 16:
            self.logger.info("Invalid IV")
            return None

        cipher = AES.new(self.sharedkey[ node ], AES.MODE_CBC, iv )
        return pkcs7_unpad(cipher.decrypt(cyphertext).decode())
        
    except Exception as e:
        self.logger.error("Error in decryption: {}".format(str(e)))
        return None
        
  def announce(self,dstnode):
    self.send_tofu('announce', dstnode)

  def welcome(self,dstnode):
    self.send_tofu('welcome', dstnode)

  def cmd_welcome(self, msg):
    # self.tofu(msg)
    pass

  def cmd_announce(self,msg):
    self.welcome(msg['node'])
    return super().cmd_announce(msg)

# Allow this class to auto instanciate if
# we run it on its own.
#
if __name__ == "__main__":
  tofc = TrustOnFirstContact()
  if not tofc:
    sys.exit(1)
  exitcode = tofc.run()
  sys.exit(exitcode)

