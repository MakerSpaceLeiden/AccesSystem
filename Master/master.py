#!/usr/bin/env python

import os
import sys
import time
import hmac
import hashlib

sys.path.append('.')
import db

sys.path.append('../lib')
import DrumbeatNode as DrumbeatNode
import AlertEmail as AlertEmail
import PingNode as PingNode

class Master(db.TextDB, DrumbeatNode.DrumbeatNode, AlertEmail.AlertEmail,PingNode.PingNode):
  default_subject="[Master ACNode]"
  default_email = None

  def __init__(self):
    super().__init__()

    # Requests from the field
    #
    self.commands[ 'open' ] = self.cmd_approve
    self.commands[ 'energize' ] = self.cmd_approve
    self.commands[ 'lastused' ] = self.cmd_lastused
    # self.commands[ 'beat' ] = self.cmd_beat

    # self.commands[ 'event' ] = self.cmd_event

  def parseArguments(self):
    self.parser.add('--secrets', action='append',
         help='Secret pairs for connecting nodes in nodename=secret style.')

    super().parseArguments()

    # Parse the secrets into an easier access table. Fall
    # back to the global secret if no per-node one defined.
    #
    if self.cnf.secrets:
       newsecrets = {}
       for e in self.cnf.secrets:
         secret = self.cnf.secret
         if e.find('=') > 0:
           node, secret = e.split('=',1)
         else:
           node = e
           secret = self.cnf.secret
         newsecrets[ node ] = secret

       self.cnf.secrets = newsecrets

  def announce(self, dstnode):
    # We announce to all our constituents (a normal node just
    # Announces to the master). If needed - this can trigger
    # the nodes to do things like wipe a cache, sync time, etc.
    #
    if dstnode == self.cnf.master:
       for dstnode in self.cnf.secrets:
          self.announce(dstnode)
       return

    super().announce(dstnode)

  # Handle a note reporting back the most recently swiped
  # node -- generally in response to a reveal-tag command
  # from us.
  #
  def cmd_lastused(self,msg):
    cmd, tag = self.split_payload(msg)
    if not cmd or not tag:
      return

    self.logger.info("Unknown tag {} reportedly used at {}".format(tag,msg['node']))

    self.rat(msg,tag)

  def rat(self,msg,tag):
    body = "An unknown tag ({}) was reportedly used at node {} around {}.".format(tag,msg['node'],time.asctime())
    subject = "Unknown tag {} used at {}".format(tag,msg['node'])
    self.send_email(body, subject)

  def cmd_approve(self,msg):
    cmd, target_node, target_machine, tag_encoded = self.split_payload(msg) or (None, None, None, None)
    
    if not target_node:
       return

    if 'hdr' in msg and msg[ 'hdr' ] == 'SIG/2.0':
      tag = self.session_decrypt(msg, tag_encoded)

      if not tag in self.userdb:
        self.logger.info("Tag {} not in DB; reporting.".format(tag))
        self.rat(msg, tag)
        return
    else:
      tag = None
      secret = self.secret( msg['node'] )
      if not secret:
          self.logger.error("No secret for node '{}' (but how did we ever get here?".format(msg['node']))
          return

      # Finding the tag is a bit of a palaver; we send the tag over the wired in a hashed
      # format; with a modicum of salt. So we need to compare that hash with each tag
      # we know about after salting that key. 
      #
      for uid in self.userdb.keys():
        tag_hmac = hmac.new(secret.encode('ASCII'),msg['theirbeat'].encode('ASCII'),hashlib.sha256)
        try:
          if sys.version_info[0] < 3:
             tag_asbytes= ''.join(chr(int(x)) for x in uid.split("-"))
          else:
             tag_asbytes = bytearray(map(int,uid.split("-")))
        except:
          self.logger.error("Could not parse tag '{0}' in config file-- skipped".format(uid))
          continue
  
        tag_hmac.update(bytearray(tag_asbytes))
        tag_db = tag_hmac.hexdigest()

        if tag_encoded == tag_db:
           tag = uid
           break

      if not tag in self.userdb:
        self.logger.info("Tag not in DB; asking node to reveal it")
        self.send(msg['node'],"revealtag")
        return

    acl = 'error'
    email = self.userdb[tag]['email'];
    name = self.userdb[tag]['name'];

    if target_machine in self.userdb[tag]['access']:
         self.logger.info("tag '%s' (%s) OK for action: '%s' on '%s'", tag, name, target_machine, target_node);
         acl = 'approved'
    else:
         self.logger.info("tag '%s' (%s) denied action: '%s' on '%s'", tag, name, target_machine, target_node);
         acl = 'denied'
   
    self.send(target_node, acl + ' ' + cmd + ' ' +  target_machine + ' ' + msg['theirbeat'])

  # XXX: at some point we could break this out and get nice, per node, logfiles.
  #
  def cmd_event(self,msg):
     cmd, info = msg['payload'].split(' ',1)
     self.logger.info("Node {} Event: {}".format(msg['node'],info))

  def cmd_beatOFF(self,msg):
    if 'theirbeat' in msg and msg['theirbeat'] < 600:
       self.logger.info("Spotted a node that has been started very recently - sending it an announce.")
       self.announce(msg['node']) 
       return

    return super().cmd_beat(msg);


master = Master()

if not master:
  sys.exit(1)

exitcode = master.run()
sys.exit(exitcode)



