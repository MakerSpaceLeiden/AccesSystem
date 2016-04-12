#!/usr/bin/env python3.4

import os
import sys
import time
import hmac
import hashlib

sys.path.append('.')
import db

sys.path.append('../lib')
import alertEmail
import DrumbeatNode as DrumbeatNode

class Master(db.TextDB, DrumbeatNode.DrumbeatNode):
  default_subject="[Master ACNode]"
  default_email = None

  def __init__(self):
    super().__init__()
    self.commands[ 'open' ] = self.cmd_approve
    self.commands[ 'energize' ] = self.cmd_approve
    self.commands[ 'lastused' ] = self.cmd_lastused

  def parseArguments(self):
    self.parser.add_argument('-C', nargs=2, metavar=('tag', 'machine'),
                   help='Client mode - useful for testing')

    self.parser.add('--subject',default=self.default_subject,
         help='Subject prefix for alert emails (default: '+self.default_subject+')'),

    self.parser.add('--secrets', action='append',
         help='Secret pairs for connecting nodes in nodename=secret style.')

    self.parser.add('--email',
         help='Email address for alerts (default is none)'),
    
    super().parseArguments()

    if self.cnf.secrets:
       newsecrets = {}
       for e in self.cnf.secrets:
         node, secret = e.split('=',1)
         newsecrets[ node ] = secret
       self.cnf.secrets = newsecrets

  def announce(self,dstnode):
    for dstnode in self.cnf.secrets:
       self.send(dstnode, "announce")

  def cmd_lastused(self,path,node,theirbeat,payload):
    try:
      cmd, tag = payload.split()
    except:
      self.logger.error("Could not parse lastused payload '{}' -- ignored.".format(payload))
      return

    self.logger.info("Unknown tag {} reportedly used at {}".format(tag,node))

    self.send_email("Unknown tag {} used at {}".format(tag,node),
	"An unknown tag ({}) was reportedly used at node {} around {}.".format(tag,node,time.asctime()))

  def cmd_approve(self,path,node,theirbeat,payload):
    cmd, target_node, target_machine, tag_encoded = self.parse_request(payload) or (None, None, None, None)
    if not target_node:
       return

    tag = None
    secret = self.secret( node )
    if not secret:
        self.logger.error("No secret for node '{}' (but how did we ever get here?".format(node))
        return
 
    for uid in self.userdb.keys():

      tag_hmac = hmac.new(secret.encode('ASCII'),theirbeat.encode('ASCII'),hashlib.sha256)
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
      self.send(node,"revealtag")
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
    
    self.send(target_node, acl + ' ' + cmd + ' ' +  target_machine + ' ' + theirbeat)

master = Master()

if not master:
  sys.exit(1)

if False:
   print("Client mode - one post shot.")
   tag,machine = args.C
   topic = cnf['mqtt']['sub'] + '/master'

   data = 'energize ' + machine + ' ' + tag

   if not machine in cnf['secrets']:
     self.logger.error("No secret defined, Aborting")
     sys.exit(1)

   secret = cnf['secrets'][machine]
   nonce = hashlib.sha256(os.urandom(1024)).hexdigest()
   payload = data

   HMAC = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
   HMAC.update(payload.encode('ASCII'))
   hexdigest = HMAC.hexdigest()

   data = "SIG/1.00 " + hexdigest + " " + nonce + " " + machine + " " + payload
    
   self.logger.info("@"+topic+": " + data) 
   publish.single(topic, data, hostname=cnf['mqtt']['host'], protocol="publish.MQTTv311")



exitcode = master.run()

sys.exit(exitcode)



