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

import configargparse

import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish

import MqttHandler

default_cnf_files = ["/usrlocal/etc/acnode.ini","/etc/acnode.ini","~/.acnode.ini", "acnode.ini"]
default_master = 'master'
default_drumbeat= 'master'
default_node  = 'unamednode'
default_secret = 'public'
default_host = 'localhost'
default_sub = default_node
default_protocol = "publish.MQTTv311"
default_machine = 'deur'
default_leeway = 30

class ACNode:
  cnf = None
  topic = None
  logtopic = None
  logger = None
  protocol = None
  client = None
  parser = None
  commands = {}
  beatoff = 0
  beatsseen = 0
  forever = 0
  default_pidfile = "/var/run/master.pid"

  def __init__(self,description='ACNode', cnf_file=None):

    files = default_cnf_files
    if cnf_file: 
      files = (cnf_file)
    self.parser = configargparse.ArgParser(default_config_files=files)

    self.commands[ 'announce' ] = self.cmd_announce
    self.commands[ 'beat' ] = self.cmd_beat

  def parseArguments(self):
    self.parser.add('-c', '--config', is_config_file=True,  
         help='config file path (default is '+",".join(default_cnf_files)+').')

    self.parser.add('--master','-M',default=default_master,
         help='Name of the master node (default: '+default_master+')'),
    self.parser.add('--drumbeat','-D',default=default_drumbeat,
         help='Name of the drumbeat node (default: '+default_drumbeat+')'),

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
         help='Verbose on (default off)')
    self.parser.add('--debug', '-d', action='count', default=0,
         help='Debuging on; implies verbose (default off)')
    self.parser.add('--no-mqtt-log', action='count',
         help='Disable logging to MQTT log channel (default on)'),
    self.parser.add('--no-syslog',  action='count',
        help='Disable syslogging (defautl on)'),
    self.parser.add('-l','--logfile', type=configargparse.FileType('w+'), 
        help='Append log entries to specified file (default: none)'),

    self.parser.add('--ignorebeat', action='count',
         help='Ignore the beat (default is to follow)')
    self.parser.add('--leeway', action='store', default=default_leeway, type=int,
         help='Beat leeway, in seconds (default: '+str(default_leeway)+' seconds).')

    self.parser.add('--pidfile', action='store', default = self.default_pidfile,
         help='File to write PID to, (Default: '+self.default_pidfile+').')
    self.parser.add('--daemonize', '-b', action='count',
         help='Deamonize into the background after startup (default is to stay in the foreground).')

    self.cnf = self.parser.parse_args()

    self.cnf.follower = not self.cnf.ignorebeat

  def setup(self):
    loglevel=logging.ERROR

    if self.cnf.verbose:
      loglevel=logging.INFO

    if self.cnf.debug:
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

    # self.topic = self.cnf.topic+ "/" + self.cnf.master + "/" + self.cnf.node

  def secret(self, node = None):
    if not node or node == self.cnf.master or node == self.cnf.node:
       return self.cnf.secret

    if 'secrets' in self.cnf and node in self.cnf.secrets:
       return self.cnf.secrets[node]

    return None

  def beat(self):
      return "{:012d}".format(int(0.5 + time.time() + self.beatoff))

  def send_request(self, command, target_node, target_machine, tag_uid, beat = None):
      if not beat:
         beat = self.beat()

      tag_hmac = hmac.new( self.cnf.secret.encode('ASCII'), beat.encode('ASCII'), hashlib.sha256)
      tag_hmac.update(bytearray(tag_uid)) # note - in its original binary glory and order.
      tag_encoded = tag_hmac.hexdigest()

      data = command + " " + target_node + " " + target_machine+ " " + tag_encoded

      self.send(self.cnf.master, data, beat)
 
  def parse_request(self, payload):
    command = None
    target_node = None
    target_machine = None
    tag_encoded = None

    try:
      elems = payload.split()
      command = elems.pop(0)

      if elems:
        target_node = elems.pop(0)
      if elems:
        target_machine = elems.pop(0)
      if elems:
        tag_encoded = elems.pop(0)
      if elems:
        self.logger.info("Too many elements; ignored")
        return
    except:
      self.logger.info("Cannot parse request '{}' ; ignored".format(payload))
      return

    return(command, target_node, target_machine, tag_encoded)

 
  def hexdigest(self,secret,beat,topic,dstnode,payload):

    HMAC = hmac.new(secret.encode('ASCII'),beat.encode('ASCII'),hashlib.sha256)
    HMAC.update(topic.encode('ASCII'))
    HMAC.update(payload.encode('ASCII'))

    return HMAC.hexdigest()

  def send(self,dstnode,payload, beat= None):
      if not beat:
         beat = self.beat()

      dstsecret = self.secret(dstnode)
      if not dstsecret:
         self.logger.error("No secret defined for '{}' - aborting send".format(dstnode))
         return

      topic = self.cnf.topic+ "/" + dstnode + "/" + self.cnf.node 

      hexdigest = self.hexdigest(dstsecret,beat,topic,dstnode,payload)

      data = "SIG/1.00 " + hexdigest + " " + beat + " " + payload 

      self.logger.debug("Sending @"+topic+": "+data)
      publish.single(topic, data, hostname=self.cnf.mqtthost, protocol=self.cnf.mqttprotocol)

  def announce(self,dstnode):
    return self.send(dstnode, "announce")

  def on_connect(self, client, userdata, flags, rc):
    self.logger.info("(re)Connected to '" + self.cnf.mqtthost + "'")
    if self.cnf.node == self.cnf.master:
      self.subscribe(client,self.cnf.node + "/#" )
    else:
      self.subscribe(client,self.cnf.node + "/" + self.cnf.master)

    if self.cnf.master != self.cnf.drumbeat:
      self.subscribe(client,self.cnf.drumbeat + '/#')

  def subscribe(self,client,leaf):
    topic = self.cnf.topic + "/" + leaf

    if sys.version_info[0] < 3:
       topic = topic.encode('ASCII')

    mid = client.subscribe(topic)
    self.logger.debug(("Subscription req to {0} MID={1}".format(topic, mid)))

  def on_subscribe(self, client, userdata, mid, granted_qos):
    self.logger.info("(re)Subscribed.")
    self.announce(self.cnf.master)

  def parse_topic(self, topic):
    try:
      path = topic.split('/')
      destination = path[-2]
      node = path[-1]
    except:
      self.logger.info("Message topic '{0}' could not be parsed -- ignored.".format(topic))
      return None, None, None

    if destination == self.cnf.drumbeat and node == self.cnf.drumbeat:
      if not self.cnf.follower: 
        self.logger.debug("Ignoring drumbeat messages, not a follower.")
        return None, None, None
    else:
      if destination != self.cnf.node:
        self.logger.info("Message addressed to '{0}' not to me ('{1}') -- ignored."
           .format(destination,self.cnf.node))
        return None, None, None

    return path, destination, node

  def on_message(self,client, userdata, message):
    topic = message.topic
    self.logger.debug("@%s: : %s",topic, message.payload)

    path, moi, node = self.parse_topic(topic)
    if not path:
       self.logger.info("No path in topic '{0}' -- ignored".format(message.topic))
       return None

    payload = None
    try:
      payload = message.payload.decode('ASCII')
    except:
      self.logger.info("Non ascii equest '{0}' -- ignored".format(message.payload))
      return None

    topic = message.topic

    if not payload.startswith("SIG/"):
        self.logger.info("Unknown version of '{0}' -- ignored".format(payload))
        return None
       
    beat = self.beat()
    try:
        hdr, sig, theirbeat, payload = payload.split(' ',3)
        delta = abs(int(theirbeat) - int(beat))
    except:
        self.logger.info("Could not parse '{0}' -- ignored".format(payload))
        return None

    secret = self.secret(node)
    if not secret:
       self.logger.error("No secret defined for '{}' - ignored".format(dstnode))
       return None

    if delta > self.cnf.leeway:
        self.logger.critical("Beats are {} seconds off (max leeway is {} seconds). ignoring.".format(delta,self.cnf.leeway))
        return None
       
    hexdigest = self.hexdigest(secret,theirbeat,topic,node,payload)
    if not hexdigest == sig:
        self.logger.warning("Invalid signatured; ignored.")
        return None

    self.logger.debug("Good message.")

    cmd = payload.split(' ')[0]
    if cmd in self.commands:
        self.logger.debug("Handling command '{}' with {}:{}()".format(cmd,self.commands[cmd].__class__.__name__, self.commands[cmd].__name__))
        return self.commands[cmd](path,node,theirbeat,payload)

    self.logger.debug("No mapping for {} - deferring to <{}> for handling by {}".format(cmd, payload,self.__class__.__name__))
    return payload

  def cmd_announce(self,path,node,theirbeat,payload):
    if node != self.cnf.node:
       self.logger.info("Announce of {}".format(node))
    else:
       self.logger.info("Ignoring my own restart message.")
    return None

  def cmd_beat(self,path,node,theirbeat,payload):
    delta = abs(int(theirbeat) - int(self.beat()))

    self.logger.debug("Drumbeat - delta is {}".format(delta))
    self.beatsseen+=1

    if node == self.cnf.node:
       if delta > 5:
          self.logger.critical("My own beat is returned with more than 5 seconds delay (or getting replayed)")
       return

    if not self.cnf.follower or delta < self.cnf.leeway / 4:
       self.logger.debug("Not adjusting beat - in acceptable range.")
       return

    if delta < self.cnf.leeway * 4:
       if self.beatsseen == 1:
         self.logger.warning("About {} seconds askew; adjusting clock".format(int(delta)))
         self.beatoff -= delta
         return

       self.logger.warning("Delta too far to adjust; ignoring".format(int(delta)))
       return

    return None

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

  def initialize(self):
    self.parseArguments()
    self.setup()
    self.connect()

  def run(self):
    self.initialize()

    self.forever = 1

    if self.cnf.daemonize:
        daemon.daemonize(self.cnf.pidfile)

    self.logger.debug("Entering main forever loop.")
    while(self.forever): 
      self.loop()

    self.logger.debug("Aborting loop.")
    e = self.on_exit(self.err)

    return e 
