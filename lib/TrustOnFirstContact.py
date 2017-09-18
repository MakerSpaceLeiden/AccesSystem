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

# For older version of python use:
# from Cryptodome import Random
from Crypto import Random
import SharedSecret
import Beat

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

       randm32 = Random.new().read(32)
       self.cnf.privatekey = curve.generatePrivateKey(randm32)

       if self.cnf.privatekeyfile:
           if os.path.isfile(self.cnf.privatekey):
              self.logger.critical("Refusing to overwrite existing private key. Aborting.")
              sys.exit(1);

           if 32 != open(self.cnf.privatekeyfile,"wb").write(self.cnf.privatekey):
              self.logger.critical("Failed to write the newly generated privatekeyfile. Aborting.");
              sys.exit(1);

           self.logger.info("Wrote out newly generated private key");
       else:
           self.logger.info("Using empheral private key");

    if self.cnf.privatekey:
          # base64 decode it - and check it is 32 bytes.
         # self.cnf.privatekey = base64.b64decode(self.cnf.privatekey)

         if len(self.cnf.privatekey) != 32:
             self.logger.critical("Command line private key not exactly 32 bytes. aborting.");
             sys.exit(1);
    else:
        if self.cnf.privatekeyfile:
             self.cnf.privatekey = open(self.cnf.privatekeyfile,"rb").read(32)

             if len(self.cnf.privatekey) != 32:
                 self.logger.critical("Private key in file is not exactly 32 bytes. aborting.");
                 sys.exit(1);
      
    if self.cnf.tofconly and not self.cnf.privatekey:
         self.logger.critical("No private key - cannot do TOFC . Aborting.")
         sys.exit(1);

    self.cnf.publickey = curve.generatePublicKey(self.cnf.privatekey)
    self.pubkeys[ self.cnf.node ] =self.cnf.publickey
    
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

     randm64 = Random.new().read(64)
     signature = curve.calculateSignature(randm64, self.cnf.privatekey, payload.encode('ASCII'))
     signature = base64.b64encode(signature).decode('ASCII')

     super().send(dstnode, "SIG/2.0 "+signature+" "+payload, raw=True)

  def extract_validated_payload(self, msg):

    if not msg['payload'].startswith("SIG/2"):
        return super().extract_validated_payload(msg)

    beat = int(self.beat())
    try:
        hdr, sig, payload = msg['payload'].split(' ',2)
        sig = base64.b64decode(sig)
        
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
    if not msg['node'] in self.pubkeys:
       if cmd == 'announce' or cmd == 'welcome':
            try:
                cmd, ip, pubkey = clean_payload.split(" ")

            except Exception as e:
                self.logger.error("Error parsing announce. Ignored. "+str(e))
                return None

            publickey = base64.b64decode(pubkey)
            if len(publickey) != 32:
                    self.logger.error("Ignoring malformed public key of node {}".format(msg['node']))
                    return None

            if msg['node'] in self.pubkeys:
                if self.pubkeys[msg['node']] == publickey:
                        self.logger.debug("Ignoring (unchanged) public key of node {}".format(msg['node']))
                        return None

                self.logger.info ("Ignoring (changed) public key of node {}".format(msg['node']))
                return None

            self.logger.info("Potentially learned a public key of node {} on first contact - checking signature next.".format(msg['node']))
    else:
        publickey = self.pubkeys[ msg['node'] ]
    
    if not self.cnf.privatekey:
        return None

    # Note: this is the payload `as of now' -- not the further decoded/stripped of its beat payload.
    # Because we also (want to) have the signature cover the beat - to prevent replay.
    # Todo: consider a nonce.
    if curve.verifySignature(publickey, signed_payload.encode(), sig):
        self.logger.warning("Invalid signatured for {} - ignored".format(msg['node']))
        return None

    if not msg['node'] in self.pubkeys:
        self.pubkeys[ msg['node'] ] = publickey
        self.logger.info("Learned a public key of node {} on first contact.".format(msg['node']))

        session_key = curve.calculateAgreement(self.cnf.privatekey, publickey)
        self.sharedkey[ msg['node'] ] = hashlib.sha256(session_key).hexdigest()

        self.logger.debug("Calculated shared secret with node {}.".format(msg['node']))

    # self.logger.debug("Good signature.")
    msg['validated'] = 20
    
    return msg

  def send_tofu(self, prefix, dstnode):
    bp = base64.b64encode(self.cnf.publickey).decode('ASCII')
    return self.send(dstnode, prefix + " " + socket.gethostbyname(socket.gethostname()) + " " + bp)

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

