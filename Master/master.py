#!/usr/bin/env python

import os
import sys
import time

sys.path.append('../lib')
import alertEmail
from ACNode import ACNode

class Master(ACNode):
  default_subject="[Master ACNode]"
  default_dbfile="./dbfile"
  default_email = None
  rollingnonces = {}

  def parseArguments(self):
    self.parser.add_argument('-C', nargs=2, metavar=('tag', 'machine'),
                   help='Client mode - useful for testing')

    self.parser.add('--subject',default=default_subject,
         help='Subject prefix for alert emails (default: '+default_subject+')'),

    self.parser.add('--email',
         help='Email address for alerts (default is none)'),
    
    self.parser.add('--dbfile',default=default_dbfile,
         help='File with access database (default: '+default_dbfile+')'),
    super().parseArguments()

  def setup(self):
    super().setup()

    self.reload_db()
    signal.signal(signal.SIGHUB, self.reload_db)

  def reload_db(self):
     newuserdb = {}
     try:
       for row in open(self.cnf.dbfile,'r'):
          if row.startswith('#'): continue
          if len(row.split(':')) != 4: continue
          
          # Parse keys to valid input
          tag,access,name,email = row.strip().split(':')
          tag = '-'.join(map(str.strip, tag.split(',')))
          
          # Break access up into the things this user
          # has access to.          
          allowed_items = access.split(',')

          # Create database
          newuserdb[tag] = { 'tag': 'hidden', 'access': allowed_items, 'name': name, 'email': email }
     except IOError as e:
       self.logger.critical("I/O error %s", e.strerror())
       sys.exit(1)
     except ValueError:
       self.logger.error("Could not convert data to an integer -- some malformed tag ? ignored. ")
       raise
     except:
       self.logger.critical("Unexpected error: %s", sys.exc_info()[0])
       sys.exit(1)

     self.logger.info("Reloaded userdb")
     self.userdb = newuserdb

  def secret(node = None)
    if not node or node == self.cnf.master:
       return self.cnf.secret
        
    if node in self.cnf.secrets:
       return self.cnf.secrets[node]

    return None
 
def on_message(client, userdata, message):
    payload = super.on_message(client, userdata, message)

    try:
      dstnode, payload = payload.split(' ',2)
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

    if what == 'roll':
       self.logger.debug("Updated nonce for node '{0}' to '{1}'".format(dstnode,nonce))
       rollingnonces[node] = nonce
       return

    tag = None
    for uid in userdb.keys():

      tag_hmac = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
      try:
        tag_asbytes= ''.join(chr(int(x)) for x in uid.split("-") )
      except:
        self.logger.error("Could not parse tag '{0}' in config file-- skipped".format(uid))
        continue

      tag_hmac.update(tag_asbytes)
      tag_db = tag_hmac.hexdigest()

      if tag_encoded == tag_db:
         tag = uid
         break

    if not tag in userdb:
      self.logger.info("Unknown tag; ignored")
      return

    if not tag in userdb:
      self.logger.info("Tag not in DB; ignored")
      return

    email = userdb[tag]['email'];
    name = userdb[tag]['name'];

    acl = 'error'
    if which in userdb[tag]['access']:
       self.logger.info("tag '%s' (%s) OK for action: '%s' on '%s'", tag, name, what, which);
       acl = 'approved'
    else:
       self.logger.info("tag '%s' (%s) denied action: '%s' on '%s'", tag, name, what, which);
       acl = 'denied'
    
    if dstnode != node:
       self.logger.debug("Target node '{1}' not the same as requesting node {0} - using rolling nonce.".format(dstnode,node))
       if not dstnode in rollingnonces:
          self.logger.info("No rolling nonce for node '%s'", node)
          return
       nonce = rollingnonces[node]
    
    topic = cnf['mqtt']['sub']+'/'+dstnode+'/reply'

    msg = None
    if what == 'energize':
      msg = 'energize ' + which + ' ' + acl

    if what == 'open':
      msg = 'open ' + which + ' ' + acl

    if not msg:
      self.logger.info("Unknown commnad '{0]'- ignored.".format(what))
      return

    self.logger.info("@"+topic+": "+msg)
    self.logger.debug("Nonce: "+nonce)
    reply(topic, msg, nonce)

   if not which in cnf['secrets']:
     self.logger.warning("No secret defined to reply with -- ignoring")
     return

   if not requestnonce:
     self.logger.warning("No nonce -- ignoring")
     return

   secret = cnf['secrets'][which]

   self.reply(node


if args.C:
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
