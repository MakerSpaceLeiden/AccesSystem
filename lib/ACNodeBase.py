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

default_cnf_files = ["/usrlocal/etc/acnode.ini","/etc/acnode.ini","~/.acnode.ini", "acnode.ini"]
default_master = 'master'
default_node  = 'unamednode'
default_host = 'localhost'
default_sub = default_node
default_protocol = "publish.MQTTv311"
default_machine = 'deur'
default_leeway = 30

class ACNodeBase:
  cnf = None
  topic = None
  logtopic = None
  logger = None
  protocol = None
  client = None
  parser = None
  commands = {}
  forever = 0
  default_pidfile = "/var/run/master.pid"

  def __init__(self,description='ACNodeBase', cnf_file=None):

    files = default_cnf_files
    if cnf_file: 
      files = (cnf_file)
    self.parser = configargparse.ArgParser(default_config_files=files)

  def parseArguments(self):
    self.parser.add('-c', '--config', is_config_file=True,  
         help='config file path (default is '+",".join(default_cnf_files)+').')

    self.parser.add('--master','-M',default=default_master,
         help='Name of the master node (default: '+default_master+')'),

    self.parser.add('--node','-n',default=default_node,
         help='Name of this node (default: '+default_node+')'),
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

    self.parser.add('--leeway', action='store', default=default_leeway, type=int,
         help='Beat leeway, in seconds (default: '+str(default_leeway)+' seconds).')

    self.parser.add('--pidfile', action='store', default = self.default_pidfile,
         help='File to write PID to, (Default: '+self.default_pidfile+').')
    self.parser.add('--daemonize', '-b', action='count',
         help='Deamonize into the background after startup (default is to stay in the foreground).')

    self.cnf = self.parser.parse_args()

  def setup(self):
    setproctitle.setproctitle(self.cnf.node)

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
    signal.signal(signal.SIGINT, self.end_read)
    signal.signal(signal.SIGQUIT, self.end_read)

  def protect_uid(self,target_node, uid):
      return "**-**-**-**"

  def send_request(self, command, target_node, target_machine, tag_uid):

      tag_encoded = self.protect_uid(target_node, tag_uid)

      data = command + " " + target_node + " " + target_machine + " " + tag_encoded
      self.send(self.cnf.master, data)
 
  def parse_request(self, payload):
    command = None

    try:
      elems = payload.split()
      return(elems)

    except:
      self.logger.debug("Cannot parse payload '{}' ; ignored".format(payload))
      return None

    return None
 
  def send(self,dstnode,payload):
      topic = self.cnf.topic+ "/" + dstnode + "/" + self.cnf.node 

      self.logger.debug("Sending @"+topic+": "+payload)
      try:
         publish.single(topic, payload, hostname=self.cnf.mqtthost, protocol=self.cnf.mqttprotocol)
      except:
         self.logger.critical("Failed to send {}: '{}'".format(topic,payload));

  def announce(self,dstnode):
    return self.send(dstnode, "announce " + socket.gethostbyname(socket.gethostname()));

  def on_connect(self, client, userdata, flags, rc):
    self.logger.info("(re)Connected to '" + self.cnf.mqtthost + "'")
    if self.cnf.node == self.cnf.master:
      self.subscribe(client,self.cnf.node + "/#" )
    else:
      self.subscribe(client,self.cnf.node + "/" + self.cnf.master)

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
      self.logger.warning("Message topic '{0}' could not be parsed -- ignored.".format(topic))
      return None, None, None

    return path, destination, node

  def extract_validated_payload(self, node, topic, path, payload):
    return payload

  def on_message(self,client, userdata, message):
    topic = message.topic
    self.logger.debug("@%s: : %s",topic, message.payload)

    path, moi, node = self.parse_topic(topic)
    if not path:
       return None

    payload = None
    try:
      payload = message.payload.decode('ASCII')
    except:
      self.logger.warning("Non ascii equest '{0}' -- ignored".format(message.payload))
      return None

    topic = message.topic
    payload = self.extract_validated_payload(node, topic, path, payload)

    if not payload:
        return None

    cmd = payload.split(' ')[0]
    if cmd in self.commands:
        self.logger.debug("Handling command '{}' with {}:{}()".format(cmd,self.commands[cmd].__class__.__name__, self.commands[cmd].__name__))
        return self.commands[cmd](path,node,payload)

    self.logger.debug("No mapping for {} - deferring to <{}> for handling by {}".format(cmd, payload,self.__class__.__name__))
    return payload

  # Capture SIGINT for cleanup when the script is aborted
  def end_read(self,signal,frame):
      self.forever = 0
      self.logger.warning("Abort detected; stopping")
      self.err = 0

  def on_exit(self,e):
      self.client.disconnect()
      self.logger.info("Closed down.")
      return(e)

  def connect(self):

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

    self.logger.warning("Node {} started.".format(self.cnf.node))
    while(self.forever): 
      self.loop()

    self.logger.debug("Aborting loop.")
    e = self.on_exit(None)

    return e 
