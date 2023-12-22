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
import logging

sys.path.append('.')
import re

sys.path.append('../lib-python')
import DrumbeatNode as DrumbeatNode
import AlertEmail as AlertEmail
import PingNode as PingNode

class Master(DrumbeatNode.DrumbeatNode, AlertEmail.AlertEmail,PingNode.PingNode):
  default_subject="[Master ACNode]"
  default_email = None
  opento = []
  allopento = [] 
  recents = {}
  xscache = {}
  logger = logging.getLogger()

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
    self.parser.add('--nodes', action='append',
         help='Secret pairs for connecting nodes in nodename=secret style.')

    self.parser.add('--opento', action='append',
         help='Additional space log informants - spacedoor')

    self.parser.add('--allopento', action='append',
         help='Additional space log informants - all doors')

    self.parser.add('--bearer', action='store',
         help='File with bearer secret')

    self.parser.add('--unknown_url', action='store',
         help='URL to report unknown tags to')
    self.parser.add('--xscheck_url', action='store',
         help='URL to check machine and tag pairs')
    self.parser.add('--xscheck_node_url', action='store',
         help='URL to check what machines on a node a tag can access')

    super().parseArguments()

    # Parse the nodes into an easier access table. 
    #
    if self.cnf.nodes:
       newnodes = {}
       for node in self.cnf.nodes:
         # We used to allow the specification of shared secrets here.
         # So warn if we still see this.
         if '=' in node:
            self.logger.critical("SIG/1 and older no longer supported; no secrets in node list allowed anymore.")
            sys.exit(1)
         newnodes[ node ] = True
       self.cnf.nodes = newnodes

    if not self.cnf.nodes:
       self.logger.critical("No nodes configured. Is this correct ?!")

    if self.cnf.bearer:
        with open(self.cnf.bearer) as f:
           self.cnf.bearer = f.read().split()[0].strip()

    if not self.cnf.xscheck_url or not self.cnf.xscheck_node_url:
       self.logger.critical("No XS url configured.");

    if not self.cnf.bearer:
       self.logger.critical("No XS bearer secret configured.");

  def announce(self, dstnode):
    # We announce to all our constituents (a normal node just
    # Announces to the master). If needed - this can trigger
    # the nodes to do things like wipe a cache, sync time, etc.
    #
    if dstnode == self.cnf.master:
     if self.cnf.nodes:
       for dstnode in self.cnf.nodes:
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
    # self.rat(msg,tag)

  def rat(self,msg,tag):
    body = "An unknown tag ({}) was reportedly used at node {} around {}.".format(tag,msg['node'],time.asctime())
    subject = "Unknown tag {} used at {}".format(tag,msg['node'])
    self.send_email(body, subject)

  def decode_tag(self,msg,tag):
    if not 'hdr' in msg or not msg[ 'hdr' ] == 'SIG/2.0':
      self.logger.error("No longer supporting protocols other than SIG/2.0")
      return None

    tag = self.session_decrypt(msg, tag_encoded)
    self.logger.debug("Tag decoded: {}".format(tag))

    if not tag or tag == 'None':
        self.logger.error("Got no tag on {}. {}. {}. {}. {}. Ignoring.".format(cmd,target_node, target_machine, tag_encoded, tag))
        return None

    return tag

  def register_unknow_tag_use(self,tag):
      # self.rat(msg, tag)
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

  def get_tag_node(self, target_node, tag):
    vv = []
    try:
      url = "{}/{}".format(self.cnf.xscheck_node_url, target_node)
      r = requests.post(url, data= { 'tag' : tag }, headers = { 'X-Bearer': self.cnf.bearer })
      if r.status_code == 200:
          for target_machine, d in r.json:
               ok = d['access']
               name = d['name']
               target_machine = d['machine']
               v = { 'name': name, 'machine': target_machine, 'node': target_node, 'userid': d['userid'] }
               if d['access']:
                    v['acl'] = 'approved'
                    v['ok'] = True
               else:
                     v['acl'] = 'denied'
               vv.append(v)
               self.logger.info("REST based {} for {} on {}@{} :: {}".format(ok, name, target_machine, target_node, json.dumps(d)))
      else:
               self.logger.error("CRM http xs fetch gave a non-200 answer: {}".format(r.status_code))
               self.logger.debug('URL:{} {} {}'.format(r.url, tag, self.cnf.bearer))
    except Exception as e:
            self.logger.info("rest checkDB failed: {}".format(e))
    return vv

  def get_tag_machine(self, target_machine, target_node, tag):
    ok = False
    v = {}
    v['ok'] = False 
    ckey = '{}/{}/{}'.format(target_machine, target_node, tag)
    try:
          url = "{}/{}".format(self.cnf.xscheck_url, target_machine)
          r = requests.post(url, data= { 'tag' : tag }, headers = { 'X-Bearer': self.cnf.bearer })
          if r.status_code == 200:
               d = r.json()
               ok = d['access']
               name = d['name']
               v = { 'name': name, 'machine': target_machine, 'node': target_node, 'userid': d['userid'] }
               if ckey in self.xscache.keys():
                    del self.xscache[ ckey ]
               if ok:
                    self.xscache[ ckey ] = name
               self.logger.info("REST based {} for {} on {}@{} :: {}".format(ok, name, target_machine, target_node, json.dumps(d)))
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

    if ok:
         v['acl'] = 'approved'
         v['ok'] = True
    else:
         v['acl'] = 'denied'

    return v

  def cmd_permitted(self,msg):
    if not target_node:
       self.logger.info("Not the target node, igorning({})".format(target_node))
       return

    cmd, target_node, target_machine, tag_encoded = self.split_payload(msg) or (None, None, None, None)
    tag = decode_tag(msg,tag_encoded)
    if not tag:
        return

    vv = get_tag_node(target_node, tag)

    if not vv:
      self.logger.info("Tag {} not found either DB{}; reporting (no deny sent).".format(tag,extra_msg))
      register_tag_use(tag)
      return

    for v in vv:
       v['cmd'] = cmd
       if v['ok']:
           send_response(target_machine, target_node,tag,v)

  def cmd_approve(self,msg):
    if not target_node:
       self.logger.info("Not the target node, igorning({})".format(target_node))
       return

    cmd, target_node, tag_encoded = self.split_payload(msg) or (None, None, None)
    tag = decode_tag(msg,tag_encoded)

    if not tag:
        return
   
    v = get_tag_machine(target_machine, target_node, tag)

    if v['ok']:
      self.logger.info("Tag {} not found either DB{}; reporting (no deny sent).".format(tag,extra_msg))
      register_tag_use(tag)
      return

    v['cmd'] = cmd
    send_response(target_machine, target_node,tag,v)

  def send_response(self,target_machine, target_node,tag,v):
    self.logger.info("Member %s %s action '%s' '%s' on '%s'", v['name'], v['acl'], cmd, target_machine, target_node);
    self.logger.info('JSON={}'.format(json.dumps(v)))

    try:
        msg = "{} {} {} {}".format(v['acl'], cmd, target_machine, msg['theirbeat'])
        self.send(target_node, msg)
    except e:
        self.logger.error("Fail in send: {}".format(str(e)))

    if not v['ok']:
       body = "{} (with tag {}) was denied on machine/door {}.\n\n\nYour friendly Spacebot".format(v['name'], tag, target_machine)
       subject = "Denied {} on {} @ MSL".format(v['name'], target_machine)
       if target_machine != 'abene' and target_machine != 'grinder':
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
