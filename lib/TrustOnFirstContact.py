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
import linecache
import datetime


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
    self.commands[ 'pubkey' ] = self.cmd_pubkey

  def parseArguments(self):
    self.parser.add('--privatekey','-K',
         help='Private Curve25519 key (Default: unset, auto generated')
    self.parser.add('--privatekeyfile','-f',
         help='Private Curve25519 key file (Default: unset, auto generated')
    self.parser.add('--newprivatekey','-N',action='count',
         help='Generate a new private key, save to privatekeyfile if defined')
    self.parser.add('--tofconly','-F',action='count',
         help='Force trust-on-first-use mode (default: off)')
    self.parser.add('--trustdb','-S',
         help='Persistent trustdatabase (default: none)')

    super().parseArguments()

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
           self.cnf.privatekey = base64.b64encode(privatekey.to_seed())

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
            self.logger.critical("No seed (private key).");
            sys.exit(1);
        
    self.cnf.privatekey = ed25519.SigningKey(seed)

    if self.cnf.tofconly and not self.cnf.privatekey:
         self.logger.critical("No private key - cannot do TOFC . Aborting.")
         sys.exit(1);

    if self.cnf.trustdb:
       if not self.load_pkdb():
          sys.exit(1);

    self.cnf.publickey = self.cnf.privatekey.get_verifying_key()
    self.pubkeys[ self.cnf.node ] = self.cnf.publickey

    self.session_priv = curve.generatePrivateKey(Random.new().read(32));
    self.session_pub =  curve.generatePublicKey(self.session_priv);
    
  def send(self, dstnode, payload):
     if not self.cnf.privatekey:
        return super().send(dstnode, payload)

     payload = self.beat()+" "+payload
     signature = self.cnf.privatekey.sign(payload.encode('ASCII'))
     signature = base64.b64encode(signature).decode('ASCII')

     # self.logger.debug(" ** Payload=<{}>".format(payload.encode('ASCII')));
     # self.logger.debug(" ** Signature=<{}>".format(signature.encode('ASCII')));
     # self.logger.debug(" ** Pubkey=<{}>".format(base64.b64encode(self.cnf.publickey.to_bytes())))

     payload =  "SIG/2.0 "+signature+" "+payload

     super().send(dstnode, payload, raw=True)

  def load_pkdb(self):
    i = 0
    try:
      #with open(self.cnf.trustdb,'rt') as f:
      # for line in f:
      for line in  open(self.cnf.trustdb,'rt'):
         i = i + 1
         l = line.strip()
         if l.startswith("#") or len(l) == 0:
           continue

         (node,bs64pubkey) = line.split();
         self.pubkeys[ node ] = ed25519.VerifyingKey(bs64pubkey, encoding="base64")
      self.logger.debug("Read {} TOFU keys from {}.".format(len(self.pubkeys), self.cnf.trustdb))
      return True
    except FileNotFoundError:
      self.logger.critical("Could not find trustdb file {} (did you create it with 'touch')".
            format(self.cnf.trustdb))
    except Exception as e:
      self.logger.critical("Could not read line {} in trustdb  file {}: {}".
            format(i,self.cnf.trustdb,str(e)))
    return False

  def save_pkdb(self):
    if not self.cnf.trustdb:
      self.logger.critical("No trustdb specified.")
      sys.exit(1)
      return False

    try:
      tmpname = self.cnf.trustdb + '.new.' + str(os.getpid())
      f = open(tmpname,'xt')
      f.write("# Written {}\n#\n".format(datetime.datetime.now()))
      for node in self.pubkeys:
         f.write("{} {}\n".format(node, base64.b64encode(self.pubkeys[ node ].to_bytes()).decode('ASCII')));
      f.close();
      os.rename(tmpname, self.cnf.trustdb)
      return True 
    except Exception as e:
      self.logger.critical("Could not write trustdb file {}: {}".
            format(self.cnf.trustdb,str(e)))
      sys.exit(1)
    return False

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
    nonce = None
    if cmd == 'announce' or cmd == 'welcome':
            try:
                cmd, ip, pubkey, seskey, nonce  = clean_payload.split(" ")

            except Exception as e:
                self.logger.error("Error parsing/splitting {}: {}. Ignoring. ".format(cmd, str(e)))
                return None

            publickey = base64.b64decode(pubkey)
            if len(publickey) != 32:
                    self.logger.error("Ignoring malformed signing public key of node {}".format(msg['node']))
                    return None

            if publickey == self.cnf.publickey.to_bytes():
                    self.logger.debug("Ignoring the message - as it is my own (pubkey).")

            publickey = ed25519.VerifyingKey(pubkey, encoding="base64")
            
            seskey = base64.b64decode(seskey)
            if len(seskey) != 32:
                    self.logger.error("Ignoring malformed session public key of node {}".format(msg['node']))
                    return None

            if msg['node'] in self.pubkeys:
                if self.pubkeys[msg['node']] != publickey:
                   self.logger.critical("Ignoring (changed) public key of node {}".format(msg['node']))
                   return None
            else:
                 self.logger.debug("Potentially learned a public key of node {} on first contact - checking signature next.".format(msg['node']))
    else:
        if not msg['node'] in self.pubkeys:
            self.logger.info("No public key for node {} -- ignoring.".format(msg['node']))
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
        self.save_pkdb()  
        self.logger.info("Learned a public key of node {} on first contact.".format(msg['node']))
    else:
        if (self.pubkeys[ msg['node'] ] == publickey):
        	self.logger.debug("Already have this very public key recorded for this node {}.".format(msg['node']))
        else:
        	self.logger.warning("Does NOT match the key recorded for node {}.".format(msg['node']))

    if (cmd == 'announce' or cmd == 'welcome'):
        if (seskey): 
            session_key = curve.calculateAgreement(self.session_priv, seskey)
            self.sharedkey[ msg['node'] ] = hashlib.sha256(session_key).digest()
            if (nonce and msg['node'] !=  self.cnf.node):
                    self.send_tofu('welcome', msg['node'], nonce)

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

  def send_tofu(self, prefix, dstnode, nonce = None):
    bp = self.cnf.publickey.to_bytes();
    bp = base64.b64encode(bp).decode('ASCII')
    sp = base64.b64encode(self.session_pub).decode('ASCII')
    if not nonce:
          nonce = base64.b64encode(Random.new().read(32)).decode('ASCII')
    if nonce: nonce = " " + nonce

    return self.send(dstnode, prefix + " " + socket.gethostbyname(socket.gethostname()) + " " + bp + " " + sp + nonce)


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
        self.logger.error("Error in decryption of {}: {}".format(cyphertext, str(e)))
        return None
        
  def announce(self,dstnode):
    self.send_tofu('announce', dstnode)

  def cmd_welcome(self, msg):
    pass

  def cmd_announce(self,msg):
    return super().cmd_announce(msg)

  def cmd_pubkey(self,msg):
    cmd, nonce, node = self.split_payload(msg) or (None, None, None)

    if self.pubkeys and node in self.pubkeys.keys():
         self.logger.debug("Sending public key of {} to node {} on request.".format(node, msg['node'])); 
         b64pubkey = base64.b64encode(self.pubkeys[ node ].to_bytes()).decode('ASCII')
         self.send(msg['node'], 'trust {} {} {}'.format(nonce, node, b64pubkey))
         return

    self.logger.critical("Request for a pubkey of {} that I do not have.".format(node))

# Allow this class to auto instanciate if
# we run it on its own.
#
if __name__ == "__main__":
  tofc = TrustOnFirstContact()
  if not tofc:
    sys.exit(1)
  exitcode = tofc.run()
  sys.exit(exitcode)

