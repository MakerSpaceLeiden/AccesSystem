#!/usr/bin/env python
#
import time 
import hashlib
import json
import sys
import signal
import logging
import logging.handlers
import os
import hmac
import daemon
import setproctitle
import socket
import traceback
import linecache

import configargparse

import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish

import MqttHandler
import ACNodeBase
import Beat

default_secret = 'public'

class SharedSecret(Beat.Beat):
  def __init__(self):
    self.commands[ 'beat' ] = self.cmd_beat

    super().__init__()

  def parseArguments(self):

    self.parser.add('-s','--secret',default=default_secret,
         help='Master node shared secret (default: '+default_secret+')')

    super().parseArguments()


  def secret(self, node = None):
    if not node or node == self.cnf.master or node == self.cnf.node:
       return self.cnf.secret

    if 'secrets' in self.cnf and node in self.cnf.secrets:
       return self.cnf.secrets[node]

    return None

  def protect_uid(self,target_node, tag_uid):
      beat = self.beat()
      tag_hmac = hmac.new( self.secret(target_node).encode('ASCII'), beat.encode('ASCII'), hashlib.sha256)
      tag_hmac.update(bytearray(tag_uid)) # note - in its original binary glory and order.
      tag_encoded = tag_hmac.hexdigest()

      return tag_encoded
    
  def send(self,dstnode,payload, beat=None, secret=None, raw=False):
      if raw:
          return super().send(dstnode, payload, raw=True)

      if not secret:
         secret = self.secret(dstnode)

      if not beat:
         beat = self.beat()

      if not secret:
         self.logger.error("No secret defined for '{}' - aborting send".format(dstnode))
         return

      topic = self.cnf.topic+ "/" + dstnode + "/" + self.cnf.node 

      hexdigest = self.hexdigest(secret,beat,topic,dstnode,payload)

      data = "SIG/1.00 " + hexdigest + " " + beat + " " + payload 
      super().send(dstnode, data, raw = True)
 
  def hexdigest(self,secret,beat,topic,dstnode,payload):

    HMAC = hmac.new(secret.encode('ASCII'),beat.encode('ASCII'),hashlib.sha256)
    HMAC.update(topic.encode('ASCII'))
    HMAC.update(payload.encode('ASCII'))

    return HMAC.hexdigest()

  def extract_validated_payload(self, msg):
   try:
    if not msg['payload'].startswith("SIG/1"):
        return super().extract_validated_payload(msg)
       
    beat = int(self.beat())
    try:
        hdr, sig, payload = msg['payload'].split(' ',2)
        msg['payload'] = payload
        msg['sig'] = sig
        msg['hdr'] = hdr
        
        if not super().extract_validated_payload(msg):
           return None
    except:
        self.logger.warning("Could not parse hmac signature from payload '{0}' -- ignored".format(msg['payload']))
        return None

    secret = self.secret(msg['node'])
    if not secret:
       self.logger.error("No secret defined for '{}' - ignored".format(msg['node']))
       return None

    hexdigest = self.hexdigest(secret,msg['theirbeat'],msg['topic'],msg['node'],msg['payload'])

    if not hexdigest == sig:
        self.logger.warning("Invalid signatured; ignored: "+secret)
        return None

    self.logger.debug("Good message.")
    cmd = payload.split(' ')[0]

    if cmd == 'announce':
        self.cmd_announce(msg)
        return None

    msg['validated'] = 10
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

  def parse_topic(self, msg):
    if not super().parse_topic(msg):
        return None

    if msg['destination'] != self.cnf.node and msg['destination'] != self.cnf.drumbeat:
        self.logger.warning("Message addressed to '{0}' not to me ('{1}') -- ignored."
            .format(msg['destination'],self.cnf.node))
        return None
        
    return msg
