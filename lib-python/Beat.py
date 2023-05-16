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

default_drumbeat= 'master'
default_leeway = 30

class Beat(ACNodeBase.ACNodeBase):
  beatoff = 0
  beatsseen = 0

  def __init__(self):
    self.commands[ 'announce' ] = self.cmd_announce
    self.commands[ 'beat' ] = self.cmd_beat

    super().__init__()

  def parseArguments(self):

    self.parser.add('--drumbeat','-D',default=default_drumbeat,
         help='Name of the drumbeat node (default: '+default_drumbeat+')')
    self.parser.add('--ignorebeat', action='count',
         help='Ignore the beat (default is to follow)')
    self.parser.add('--leeway', action='store', default=default_leeway, type=int,
         help='Beat leeway, in seconds (default: '+str(default_leeway)+' seconds).')

    super().parseArguments()
    self.cnf.follower = not self.cnf.ignorebeat

  def beat(self):
      return "{:012d}".format(int(0.5 + time.time() + self.beatoff))

  def send(self,dstnode,payload, beat=None, secret=None, raw=False):
      if raw:
          return super().send(dstnode, payload, raw=True)
          
      if not beat:
          beat = self.beat()
          
      data = beat + " " + payload
      super().send(dstnode, data)
 
  def extract_validated_payload(self, msg):
   try:
    beat = int(self.beat())
    try:
        theirbeat, payload = msg['payload'].split(' ',1)
        theirbeatasint = int(theirbeat)
        delta = abs(int(theirbeat) - beat)
    except:
        self.logger.warning("Could not parse beat from payload '{0}' -- ignored".format(msg['payload']))
        return None

    if delta > self.cnf.leeway:
        if payload.find('announce') != 0:
          self.logger.critical("Beats are {} seconds off (max leeway is {} seconds). ignoring message.".format(delta,self.cnf.leeway))
          if beat < 10 * 24* 3600:
              # XXX rate limit me.
              self.logger.notice("Node in early startup spotted, sending it an announce.")
              self.announce(msg['node'])

          return None
        else:
          self.logger.info("Allowing beats to be {} seconds off on an announce msg.".format(delta,self.cnf.leeway))

    msg['theirbeat'] = theirbeat
    msg['payload'] = payload
    msg['delta'] = delta
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

  def cmd_announce(self,msg):
    if not 'delta' in msg:
        self.logger.critical("BUG")
        return None
    
    if msg['node'] == self.cnf.master and self.beatsseen < 5 and self.cnf.follower:
       if not msg['delta'] < self.cnf.leeway:
         self.logger.warning("Adjusting beat in startup window (no leeway limit), delta={} seconds".format(msg['delta']))
         self.beatoff -= msg['delta']
    
    return super().cmd_announce(msg)

  def cmd_beat(self,msg):
    # self.logger.debug("Drumbeat - delta is {}".format(msg['delta']))
    self.beatsseen+=1

    if msg['node'] == self.cnf.node:
       if msg['delta'] > 30:
           self.logger.critical("My own beat is returned with {} seconds delay (or getting replayed)".format(msg['delta']))
       return

    if not self.cnf.follower or (msg['delta'] < self.cnf.leeway / 4):
       self.logger.debug("Not adjusting beat - in acceptable range.")
       return

    if msg['delta'] < self.cnf.leeway:
       if self.beatsseen == 1:
         self.logger.warning("About {} seconds askew; adjusting clock".format(msg['delta']))
         self.beatoff -= delta
         
         # Re-announce ourselves now that we've learned about the time.
         # XXX double check the sanity of this.
         self.cmd_announce(msg)
         return

       self.logger.critical("Delta too far to adjust; ignoring.")
       return

    return None

  def on_connect(self, client, userdata, flags, rc):
    super().on_connect(client,userdata,flags,rc)

    if self.cnf.drumbeat != self.cnf.master and self.cnf.node != self.cnf.master:
        self.subscribe(client,self.cnf.drumbeat + '/' + self.cnf.drumbeat + '/#' )

  def parse_topic(self, msg):
    if not super().parse_topic(msg):
        return None

    if msg['destination'] == self.cnf.drumbeat and msg['node'] == self.cnf.drumbeat:
      if not self.cnf.follower:
        self.logger.debug("Ignoring drumbeat messages, not a follower.")
        return None
        
    return msg

