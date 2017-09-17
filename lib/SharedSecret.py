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

import configargparse

import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish

import MqttHandler
import ACNodeBase

default_secret = 'public'
default_drumbeat= 'master'

class SharedSecret(ACNodeBase.ACNodeBase):
  beatoff = 0
  beatsseen = 0

  def __init__(self):
    self.commands[ 'announce' ] = self.cmd_announce
    self.commands[ 'beat' ] = self.cmd_beat

    super().__init__()

  def parseArguments(self):

    self.parser.add('-s','--secret',default=default_secret,
         help='Master node shared secret (default: '+default_secret+')')
    self.parser.add('--drumbeat','-D',default=default_drumbeat,
         help='Name of the drumbeat node (default: '+default_drumbeat+')')
    self.parser.add('--ignorebeat', action='count',
         help='Ignore the beat (default is to follow)')

    super().parseArguments()

    self.cnf.follower = not self.cnf.ignorebeat

  def secret(self, node = None):
    if not node or node == self.cnf.master or node == self.cnf.node:
       return self.cnf.secret

    if 'secrets' in self.cnf and node in self.cnf.secrets:
       return self.cnf.secrets[node]

    return None

  def beat(self):
      return "{:012d}".format(int(0.5 + time.time() + self.beatoff))

  def protect_uid(self,target_node, uid):

      beat = self.beat()
      tag_hmac = hmac.new( self.secret(target_node).encode('ASCII'), beat.encode('ASCII'), hashlib.sha256)
      tag_hmac.update(bytearray(tag_uid)) # note - in its original binary glory and order.
      tag_encoded = tag_hmac.hexdigest()

      return tag_encoded
    
  def send(self,dstnode,payload, beat= None):
      print("+++SEND"+payload)
      if not beat:
         beat = self.beat()

      dstsecret = self.secret(dstnode)
      if not dstsecret:
         self.logger.error("No secret defined for '{}' - aborting send".format(dstnode))
         return

      topic = self.cnf.topic+ "/" + dstnode + "/" + self.cnf.node 

      hexdigest = self.hexdigest(dstsecret,beat,topic,dstnode,payload)

      data = "SIG/1.00 " + hexdigest + " " + beat + " " + payload 

      super().send(dstnode, data)
 
  def hexdigest(self,secret,beat,topic,dstnode,payload):

    HMAC = hmac.new(secret.encode('ASCII'),beat.encode('ASCII'),hashlib.sha256)
    HMAC.update(topic.encode('ASCII'))
    HMAC.update(payload.encode('ASCII'))

    return HMAC.hexdigest()

  def extract_validated_payload(self, node, topic, path, payload):

    if not payload.startswith("SIG/1"):
        self.logger.debug("Unknown version of '{0}' -- ignored".format(payload))
        return super().extract_validated_payload(node, topic, path, payload)
       
    beat = int(self.beat())
    try:
        hdr, sig, theirbeat, payload = payload.split(' ',3)
        theirbeatasint = int(theirbeat)
        delta = abs(int(theirbeat) - beat)
    except:
        self.logger.warning("Could not parse '{0}' -- ignored".format(payload))
        return None

    secret = self.secret(node)
    if not secret:
       self.logger.error("No secret defined for '{}' - ignored".format(node))
       return None

    if delta > self.cnf.leeway:
        if payload.find('announce') != 0:
          self.logger.critical("Beats are {} seconds off (max leeway is {} seconds). ignoring.".format(delta,self.cnf.leeway))
          return None
        else:
          self.logger.info("Beats are {} seconds off on an announce msg (max leeway is {} seconds).".format(delta,self.cnf.leeway))
       
    hexdigest = self.hexdigest(secret,theirbeat,topic,node,payload)

    if not hexdigest == sig:
        self.logger.warning("Invalid signatured; ignored.")
        return None

    self.logger.debug("Good message.")
    cmd = payload.split(' ')[0]

    if cmd == 'announce':
        self.cmd_announce(path,node,theirbeat,payload)
        return None

    if cmd == 'beat':
        self.cmd_beat(path,node,theirbeat,payload)
        return None
     
    return payload

  def cmd_announce(self,path,node,theirbeat,payload):
    # traceback.print_stack()

    if node != self.cnf.node:
       self.logger.info("Announce of {} {}".format(node,payload))
       if node == self.cnf.master:
           self.logger.debug("re-announce to (restarted) master")
           self.announce(self.cnf.master)
    else:
       self.logger.debug("Ignoring my own restart message.")

    if node == self.cnf.master and self.beatsseen < 2 and self.cnf.follower:
       delta = abs(int(theirbeat) - int(self.beat()))
       if not delta < self.cnf.leeway:
         self.logger.warning("Adjusting beat in startup window (no leeway limit), delta={} seconds".format(delta))
         self.beatoff -= delta
       return

    return None

  def cmd_beat(self,path,node,theirbeat,payload):
    delta = abs(int(theirbeat) - int(self.beat()))

    self.logger.debug("Drumbeat - delta is {}".format(delta))
    self.beatsseen+=1

    if node == self.cnf.node:
       if delta > 5:
          self.logger.critical("My own beat is returned with more than 5 seconds delay (or getting replayed)")
       return

    if not self.cnf.follower or (delta < self.cnf.leeway / 4):
       self.logger.debug("Not adjusting beat - in acceptable range.")
       return

    if delta < self.cnf.leeway:
       if self.beatsseen == 1:
         self.logger.warning("About {} seconds askew; adjusting clock".format(int(delta)))
         self.beatoff -= delta
         return

       self.logger.critical("Delta too far to adjust; ignoring".format(int(delta)))
       return

    return None

  def on_connect(self, client, userdata, flags, rc):

    super().on_connect(client,userdata,flags,rc)

    if self.cnf.master != self.cnf.drumbeat:
      self.subscribe(client,self.cnf.drumbeat + '/#')

  def parse_topic(self, topic):
    path, destination, node = super().parse_topic(topic)

    if destination == self.cnf.drumbeat and node == self.cnf.drumbeat:
      if not self.cnf.follower:
        self.logger.debug("Ignoring drumbeat messages, not a follower.")
        return None, None, None
    else:
      if destination != self.cnf.node:
        self.logger.warning("Message addressed to '{0}' not to me ('{1}') -- ignored."
           .format(destination,self.cnf.node))
        return None, None, None

    return path, destination, node
