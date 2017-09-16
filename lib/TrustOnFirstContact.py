#!/usr/bin/env python3.4
#

import time
import sys
import os
import base64 
import socket

import axolotl_curve25519 as curve

# For older version of python use:
# from Cryptodome import Random
from Crypto import Random

from ACNode import ACNode

class TrustOnFirstContact(ACNode):

  def __init__(self):
    super().__init__()

    self.commands[ 'announce' ] = self.cmd_announce

  def setup(self):
    super().setup()

    if self.cnf.newprivatekey:
       if self.cnf.privatekey:
              self.logger.critical("Either generate a new key; or set one. Not both. Aborting.")
              sys.exit(1);

       self.logger.info("Regenerating private key");

       randm32 = Random.new().read(32)
       self.cnf.privatekey = curve.generatePrivateKey(randm32)
       self.cnf.publickey = curve.generatePublicKey(self.cnf.privatekey)

       if self.cnf.privatekeyfile:
           if 32 != open(self.cnf.privatekeyfile,"wb").write(self.cnf.privatekey):
              self.logger.critical("Failed to write the privatekeyfile");
              sys.exit(1);

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

  def announce(self,dstnode):
    if self.cnf.privatekey:
        bp = base64.b64encode(self.cnf.privatekey).decode('ASCII')
        return self.send(dstnode, "announce " + socket.gethostbyname(socket.gethostname()) + " " + bp)

    return super().announce(dstnode)

  def cmd_announce(self,path,node,theirbeat,payload):
    try:
       cmd, ip, pubkey = payload.split(" ")
       pubkey = base64.b64decode(pubkey)
       if len(pubkey) != 32:
             self.logger.error("Ignoring malformed private key of node {}".format(node))
             return None

       if self.pubkeys[ node ]:
          if self.pubkey[node] == pubkey:
                  self.logger.debug("Ignoring (unchanged) private key of node {}".format(node))
                  return super().cmd_announce()
                  return None

          self.logger.info ("Ignoring (changed) private key of node {}".format(node))
          return None

       self.logger.info("Learned a private key of node {} on first contact.".format(node))
       self.pubkeys[ node ] = pubkey
    except:
       self.logger.error("Error parsing announce. Ignored.");
       return None

    return super().cmd_announce()

# Allow this class to auto instanciate if
# we run it on its own.
#
if __name__ == "__main__":
  tofc = TrustOnFirstContact()
  if not tofc:
    sys.exit(1)
  exitcode = tofc.run()
  sys.exit(exitcode)

