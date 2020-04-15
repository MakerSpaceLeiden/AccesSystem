#!/usr/bin/env python

import os
import sys
import time
import hmac
import hashlib
import logging
import traceback
import json
import datetime
import requests

sys.path.append('.')
import db
import re

sys.path.append('../lib-python')
import DrumbeatNode as DrumbeatNode
import AlertEmail as AlertEmail
import PingNode as PingNode

class Master(db.TextDB, DrumbeatNode.DrumbeatNode, AlertEmail.AlertEmail,PingNode.PingNode):
  default_subject="[Master ACNode]"
  default_email = None
  opento = []
  allopento = [] 
  recents = {}
  xscache = {}

  def __init__(self):
    super().__init__()

    # Requests from the field
    #
    self.commands[ 'open' ] = self.cmd_approve
    self.commands[ 'energize' ] = self.cmd_approve
    self.commands[ 'lastused' ] = self.cmd_lastused
    self.commands[ 'leave' ] = self.cmd_approve

    # self.commands[ 'event' ] = self.cmd_event
    self.cnx = None

  def parseArguments(self):
    self.parser.add('--secrets', action='append',
         help='Secret pairs for connecting nodes in nodename=secret style.')

    self.parser.add('--opento', action='append',
         help='Additional space log informants - spacedoor')

    self.parser.add('--allopento', action='append',
         help='Additional space log informants - all doors')

    self.parser.add('--bearer', action='store',
         help='Bearer secret file')

    self.parser.add('--unknown_url', action='store',
         help='Bearer URL')
    self.parser.add('--xscheck_url', action='store',
         help='Bearer URL')

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

    if self.cnf.bearer:
        with open(self.cnf.bearer) as f:
           self.cnf.bearer = f.read().split()[0].strip()

  def announce(self, dstnode):
    # We announce to all our constituents (a normal node just
    # Announces to the master). If needed - this can trigger
    # the nodes to do things like wipe a cache, sync time, etc.
    #
    if dstnode == self.cnf.master:
     if self.cnf.secrets:
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
       self.logger.info("Not the target node, igorning({})".format(target_node))
       return

    tag = None
    if 'hdr' in msg and msg[ 'hdr' ] == 'SIG/2.0':
      tag = self.session_decrypt(msg, tag_encoded)
      self.logger.debug("Tag decoded: {}".format(tag))
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

      if not tag:
        self.logger.info("Tag not in DB; asking node to reveal it")
        self.send(msg['node'],"revealtag")
        return

    acl = 'error'
    ok = False
    found = False
    v = {}
    extra_msg = ''
    ckey = '{}/{}/{}'.format(target_machine, target_node, tag)

    if self.cnf.bearer and self.cnf.xscheck_url:
        try:
          url = "{}/{}".format(self.cnf.xscheck_url, target_machine)
          r = requests.post(url, data= { 'tag' : tag }, headers = { 'X-Bearer': self.cnf.bearer })
          if r.status_code == 200:
               d = r.json()
               ok = d['access']
               name = d['name']
               v = { 'name': name, 'machine': target_machine, 'node': target_node }
               if ckey in self.xscache.keys():
                    del self.xscache[ ckey ]
               if ok:
                    self.xscache[ ckey ] = name
               self.logger.debug("REST based {} for {} on {}@{}".format(ok, name, target_machine, target_node))
          else:
               self.logger.error("CRM http xs fetch gave a non-200 answer: {}".format(r.status_code))
               self.logger.debug('URL:{} {} {}'.format(r.url, tag, self.cnf.bearer))
        except Exception as e:
            self.logger.info("rest checkDB failed: {}".format(e))

            if ckey in self.xscache.keys():
                ok = True
                name = self.xscache[ ckey ]
                self.logger.log("And using cached result to approve.")
                v = { 'name': name, 'machine': target_machine, 'node': target_node }

    if v:
        found = True
    elif tag in self.userdb:
        email = self.userdb[tag]['email'];
        name = self.userdb[tag]['name'];
        v = { 'userid': 0, 'name': name, 'email': email, 'machine': target_machine }
        found = True
        if target_machine in self.userdb[tag]['access']:
            ok = True
            self.logger.info("textfile DB approved (falltrough)")

    if not found:
      self.logger.info("Tag {} not found either DB{}; reporting (no deny sent).".format(tag,extra_msg))
      self.rat(msg, tag)
      try:
          url = "{}".format(self.cnf.unknown_url)
          r = requests.post(url, data= { 'tag' : tag }, headers = { 'X-Bearer': self.cnf.bearer })
          if r.status_code == 200:
              self.logger.debug("rest reported unknwon tag {}".format(tag))
          else:
              self.logger.info("Rest reporting of unknwon tag {} failed: {}".format(tag, r.status_code))
              self.logger.debug('URL:{} {} {}'.format(r.url, tag, self.cnf.bearer))
      except Exception as e:
          self.logger.info("rest report unknown tag {} failed: {}".format(tag, e))
      return

    if ok:
         v['acl'] = 'approved'
    else:
         v['acl'] = 'denied'
  
    v['cmd'] = cmd

    self.logger.info("Member %s %s action '%s' '%s' on '%s'", v['name'], v['acl'], cmd, target_machine, target_node);
    self.logger.info('JSON={}'.format(json.dumps(v)))

    try:
        msg = "{} {} {} {}".format(v['acl'], cmd, target_machine, msg['theirbeat'])
        self.send(target_node, msg)
    except e:
        self.logger.error("Fail in send: {}".format(str(e)))

    if not ok:
       body = "{} (with tag {}) was denied on machine/door {}.\n\n\nYour friendly Spacebot".format(v['name'], tag, target_machine)
       subject = "Denied {} on {} @ MSL".format(v['name'], target_machine)
       self.send_email(body,subject)
        # self.rat("{} denied on {}".format(v['name'], target_machine), tag)
       return

    dst = []
    if len(self.cnf.allopento):
        dst.extend(self.cnf.allopento)

    # Temp hack (famous last words)
    if target_machine == 'spacedeur':
        now = datetime.datetime.now()
        if not v['name'] in self.recents or (now - self.recents[v['name']]).total_seconds() > 60*20:
            dst.extend(self.cnf.opento)
        else:
            self.logger.info("List email not sent -- just seen that person < 20 mins before.".format(dst))
        self.recents[v['name']] = now

    if dst:
       body = "{} has just now opened the {}.\n\n\nYour friendly Spacebot".format(v['name'], target_machine)
       subject = "{} @ MSL".format(v['name'])
       self.send_email(body,subject,dst)
       self.logger.debug("Email to {} sent.".format(dst))

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

try:
        exitcode = master.run()
        sys.exit(exitcode)
except (KeyboardInterrupt, SystemExit):
        sys.exit(1)
except Exception as e:
        subject = "Exception in main run loop: {}, restarting".format(str(e))
        msg = "Tacktrace: " + traceback.format_exc()
        master.logger.critical(subject)
        print(msg)
        master.send_email(msg, subject)

sys.exit(-1)
