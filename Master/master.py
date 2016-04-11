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

class Master(db.TextDB):
  default_subject="[Master ACNode]"
  default_email = None
  rollingnonces = {}

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

  def on_message(self,client, userdata, message):
    path, moi, node = self.parse_topic(message.topic)

    if not path:
       return None

    payload = super().on_message(client, userdata, message)
    if not payload:
       return

    try:
      dstnode, payload = payload.split(' ',1)
    except:
      self.logger.info("Could not parse '{0}' -- ignored".format(payload))
      return

    which = None
    tag_encoded = None    
    try:
      elems = payload.split()
      what = elems.pop(0)
      if elems:
        which = elems.pop(0)
      if elems:
        tag_encoded = elems.pop(0)
      if elems:
        raise "Too many elements"
    except:
      self.logger.info("Cannot parse payload; ignored")
      raise
      return

    if what == 'unknowntag':
       self.logger.info("Unknown tag offered at station {}: {}".format(node,tag_encoded))
       return

    tag = None
    secret = self.secret( dstnode )
    nonce  = self.getnonce( node )

    for uid in self.userdb.keys():

      tag_hmac = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
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
      self.send(dstnode,"revealtag")
      return

    acl = 'error'
    email = self.userdb[tag]['email'];
    name = self.userdb[tag]['name'];

    if which in self.userdb[tag]['access']:
         self.logger.info("tag '%s' (%s) OK for action: '%s' on '%s'", tag, name, what, which);
         acl = 'approved'
    else:
         self.logger.info("tag '%s' (%s) denied action: '%s' on '%s'", tag, name, what, which);
         acl = 'denied'
    
    msg = None
    if what == 'energize':
      msg = 'energize ' + which + ' ' + acl

    if what == 'open':
      msg = 'open ' + which + ' ' + acl

    if not msg:
      self.logger.info("Unknown command '{0]'-- ignored.".format(what))
      return

    self.send(dstnode, msg)

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



