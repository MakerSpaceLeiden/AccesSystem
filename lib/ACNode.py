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

import configargparse

import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish

import configRead
import alertEmail
import MqttHandler

default_cnf_files = ["config.ini","~/.acnode.ini","/usrlocal/etc/acnode.ini","etc/acnode.ini"]
default_master = 'master'
default_node  = 'acnode/test'
default_secret = 'public'
default_host = 'localhost'
default_sub = default_node
default_protocol = "publish.MQTTv311"
default_machine = 'deur'

class ACNode:
  cnf = None
  topic = None
  logtopic = None
  logger = None
  protocol = None
  client = None
  parser = None
  nonce = None

  def __init__(self,description='ACNode', cnf_file=None):

    files = default_cnf_files
    if cnf_file: 
      files = (cnf_file)
    self.parser = configargparse.ArgParser(default_config_files=files)

  def parseArguments(self):
    self.parser.add('-c', '--config', is_config_file=True,  
         help='config file path'),

    self.parser.add('-M','--master',default=default_master,
         help='Name of the master node (default: '+default_master+')'),
    self.parser.add('--node','-n',default=default_node,
         help='Name of this node (default: '+default_node+')'),
    self.parser.add('-s','--secret',default=default_secret,
         help='Master node shared secret (default: '+default_secret+')'),
    self.parser.add('--machine','-a',default=default_machine,
         help='Machine (default :'+default_machine+')'),

    self.parser.add('-m','--mqtthost',default=default_host,
         help='MQTT host (default :'+default_host+')'),
    self.parser.add('--mqttprotocol',default=default_protocol,
         help='MQTT protocol (default :'+default_protocol+')'),
    self.parser.add('--topic','-t',default=default_sub,
         help='MQTT topic to subcribe to for replies from the master (default: '+default_sub+').'),

    self.parser.add('--verbose', '-v', action='count', default=0,
         help='Verbose; repeat for more verbosity (default off)')
    self.parser.add('--no-mqtt-log', action='count',
         help='Disable logging to MQTT log channel (default on)'),
    self.parser.add('--no-syslog', 
        help='Disable syslogging (defautl on)'),
    self.parser.add('-l','--logfile', type=configargparse.FileType('w+'), 
        help='Append log entries to specified file (default: none)'),

    self.cnf = self.parser.parse_args()

  def setup(self):
    loglevel=logging.ERROR

    if self.cnf.verbose:
      loglevel=logging.INFO
      if self.cnf.verbose > 1:
        loglevel=logging.DEBUG

    self.logger = logging.getLogger()
    self.logger.setLevel(loglevel)

    self.logtopic = self.cnf.topic + "/log/" + self.cnf.node
    if not self.cnf.no_mqtt_log:
      self.logger.addHandler(MqttHandler.MqttHandler(
        self.cnf.mqtthost, self.logtopic, protocol=self.cnf.mqttprotocol))

    if self.cnf.logfile:
      self.logger.addHandler(logging.StreamHandler(stream=self.cnf.logfile))

    if self.cnf.verbose:
       self.logger.addHandler(logging.StreamHandler())

    if not self.cnf.no_syslog:
       self.logger.addHandler(logging.handlers.SysLogHandler())

    if not self.cnf.machine:
      self.cnf.machine = self.cnf.node

    self.topic = self.cnf.topic+ "/" + self.cnf.master + "/" + self.cnf.node

  def secret(node = None):
    if not node or node == self.cnf.master:
       return self.cnf.secret

    if node in self.cnf.secrets:
       return self.cnf.secrets[node]

    return None

  def send_request(self, command, target_machine, tag_uid = None, nonce = None):
      if nonce:
         self.nonce = nonce

      if not self.nonce:
         self.nonce = ('nonce-'+hashlib.sha256(os.urandom(1024)).hexdigest())[0:15]

      tag_hmac = hmac.new( self.cnf.secret.encode('ASCII'), self.nonce.encode('ASCII'), hashlib.sha256)
      tag_hmac.update(bytearray(tag_uid)) # note - in its original binary glory and order.
      tag_encoded = tag_hmac.hexdigest()

      data = command + " " + target_machine+ " " + tag_encoded

      self.reply(self.cnf.node, self.nonce, self.cnf.secret, data)
  
  def reply(self,dstnode, dstsecret, mynonce, data, targetnode = None):
      if not targetnode:
         targetnode = self.cnf.master

      HMAC = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
      HMAC.update(data.encode('ASCII'))
      hexdigest = HMAC.hexdigest()
  
      topic = self.cnf.sub + "/" + targetnode + "/" + self.cnf.node 

      data = "SIG/1.00 " + hexdigest + " " + nonce + " " + dstnode + " " + data

      self.logger.debug("Sending @"+topic+": "+data)

      publish.single(topic, data, hostname=self.cnf.mqtthost, protocol=self.cnf.mqttprotocol)

  def roll_nonce(self,dstnode = None, secret = None):
   if not dstnode:
      dstnode = self.cnf.master
   if not secret:
      secret = self.cnf.secret

   nonce = hashlib.sha256(os.urandom(1024)).hexdigest()
   data = "roll"
   self.reply(self.cnf.master, secret, nonce, data)
   if not dstnode == self.cnf.master:
      self.nonce[ dstnode ] = nonce
   else:
     self.nonce = nonce

  def on_connect(self, client, userdata, flags, rc):
    self.logger.info("(re)Connected to '" + self.cnf.mqtthost + "'")
    topic = self.cnf.topic + "/" + self.cnf.node + "/reply"

    if sys.version_info[0] < 3:
       topic = topic.encode('ASCII')

    mid = client.subscribe(topic)
    self.logger.debug(("Subscription req to {0} MID={1}".format(topic, mid)))

  def on_subscribe(self, client, userdata, mid, granted_qos):
    self.logger.info("(re)Subscribed.")

  def on_message(self,client, userdata, message):
    topic = message.topic
    self.logger.debug("@%s: : %s",topic, message.payload)

    try:
      path = topic.split('/')
      moi = path[-2]
      node = path[-1]
    except:
      self.logger.info("Message topic '{0}' could not be parsed -- ignored.".format(topic))
      return None

    if moi != self.cnf.node:
      self.logger.info("Message addressed to '{0}' not to me ('{1}') -- ignored."
           .format(moi,self.cnf.node))
      return

    payload = None
    try:
      payload = message.payload.decode('ASCII')
    except:
      self.logger.info("Non ascii equest '{0}' -- ignored".format(message.payload))
      return

    topic = message.topic

    if payload.startswith("SIG/"):
      try:
        hdr, sig, payload = payload.split(' ',2)
      except:
        self.logger.info("Could not parse '{0}' -- ignored".format(payload))
        return

      if not self.cnf.secret:
        self.logger.critical("No secret configured for this node.")
        return

      secret = self.secret(node)
     
      HMAC = hmac.new(secret.encode('ASCII'),self.nonce.encode('ASCII'),hashlib.sha256)
      HMAC.update(topic.encode('ASCII'))
      HMAC.update(payload.encode('ASCII'))
      hexdigest = HMAC.hexdigest()

      if not hexdigest == sig:
        self.logger.warning("Invalid signatured; ignored.")
        return

      self.logger.debug("Good message.")

      if node == self.cnf.master and payload == 'restart':
        self.logger.info("restart of master detected; rerolling nonce")
        self.reroll_nonce()
        return None
         
      return payload

  # Capture SIGINT for cleanup when the script is aborted
  def end_read(self,signal,frame):
      self.forever = 0
      self.logger.info("Abort detected; stopping")
      self.err = 0

  def on_exit(self,e):
      self.client.disconnect()
      self.logger.info("Closed down.")
      return(e)

  def connect(self):
   signal.signal(signal.SIGINT, self.end_read)
   signal.signal(signal.SIGQUIT, self.end_read)

   try:
      self.client = mqtt.Client()
      self.client.connect(self.cnf.mqtthost)
      self.client.on_message = self.on_message
      self.client.on_connect = self.on_connect
      self.client.on_subscribe= self.on_subscribe
   except:
      self.logger.critical("MQTT connection setup to '"+self.cnf.mqtthost+"' failed:")
      if self.cnf.verbose> 1 :
        raise

      sys.exit(1)

   self.logger.debug("Setting up the connection to '"+self.cnf.mqtthost+"'")

  def loop(self):
    self.client.loop()

  def run(self):
    self.parseArguments()
    self.setup()
    self.connect()

    self.forever = 1

    self.logger.debug("Entering main forever loop.")
    while(self.forever): 
      self.loop()

    self.logger.debug("Aborting loop.")
    e = self.on_exit(self.err)

    return e 
