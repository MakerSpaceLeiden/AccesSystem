#!/usr/bin/env python
#
import time 
import hashlib
import json
import sys
import signal
import logging
import os
import hmac
import argparse

import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish

sys.path.append('../lib')
import configRead
import alertEmail

parser = argparse.ArgumentParser(description='ACNode RFID Lezer control.')
parser.add_argument('-v', action='count',
                   help='Verbose')
parser.add_argument('-c', type=argparse.FileType('r'), metavar='config.json',
                   help='Configuration file')
parser.add_argument('-s', type=str, metavar='hostname',
                   help='MQTT Server (FQDN or IP address)')
parser.add_argument('-C', nargs='+', metavar=('tags'),
                   help='Client mode - useful for testing')
parser.add_argument('--offline', action='count',
                   help='Activate offline/no-hardware needed test mode')

args = parser.parse_args()

configfile = None
if args.c:
  configfile = args.c

configRead.load(configfile)
cnf = configRead.cnf

if args.offline:
  args.v = 10

loglevel=logging.ERROR
if args.v:
  loglevel=logging.INFO
if args.v> 1:
  loglevel=logging.DEBUG

logging.basicConfig(level=loglevel)

if args.offline:
  logging.info("TEST: import MFRC522")
  logging.info("TEST: import RPI.GPIO")
else:
  # Note: The current MFC522 library claims pin22/GPIO25
  # as the reset pin -- set by the constant NRSTPD near
  # the start of the file.
  #
  import MFRC522
  MIFAREReader = MFRC522.MFRC522()
  import RPi.GPIO as GPIO

last_tag = None

master = 'master'
if 'master' in cnf:
  master = cnf['masternode']
renew = 300 # seconds

if not 'node' in cnf:
    logging.critical("No node name defined. aborting.")
    sys.exit(1)
node = cnf['node']

if not 'secret' in cnf:
    logging.critical("No secret for this node defined. aborting.")
    sys.exit(1)
secret = cnf['secret']

machine =node
if 'machine' in cnf:
  machine = cnf['machine']

topic = cnf['mqtt']['sub']+"/" + master + "/" + node

nonce_time = 0
nonce = None
# enable / disable mosfet
mosfet=1

# enbale + config stepper / disable stepper
stepper=0
steps=350

# In BCM mode
pin_dir=17     
pin_step =27
pin_enable=22
Sleep=0.0001

def init_gpio():
     if args.offline:
        logging.info("TEST: init_gpio()")
        return
     GPIO.setwarnings(False)
     GPIO.setmode(GPIO.BCM)
     if ( stepper == 1 ):
          logging.debug("setup stepper")
          GPIO.setup(pin_dir, GPIO.OUT)
          GPIO.setup(pin_step, GPIO.OUT)
          GPIO.setup(pin_enable, GPIO.OUT)
          GPIO.output(pin_enable, True)
          GPIO.output(pin_dir, False)
          GPIO.output(pin_step, False)
     if ( mosfet == 1 ):
          GPIO.setup(18, GPIO.OUT)
          GPIO.output(18, False)

def open_door():
     if args.offline:
        logging.debug("TEST: open_door()")
        return
     if ( mosfet == 1 ): 
          logging.debug("open via mosfet")
          GPIO.output(18, True)     
          time.sleep(5)
          GPIO.output(18, False)
     if (stepper == 1 ):
          logging.debug("open via stepper")
          GPIO.output(pin_enable,False)

          for step in xrange(steps):
               GPIO.output(pin_dir,False)     
               GPIO.output(pin_step,True)
               GPIO.output(pin_step,False)
               time.sleep(0.0009)
                    
          time.sleep(0.01)
          for step in xrange(steps):
               GPIO.output(pin_dir,True)     
               GPIO.output(pin_step,True)
               time.sleep(Sleep)
               GPIO.output(pin_step,False)
               time.sleep(0.0009)
                    
          GPIO.output(pin_dir,False)     
          time.sleep(0.1)
          GPIO.output(pin_enable,True)

init_gpio()
forever= True

def send_request(uid = None):
      global nonce
      nonce = hashlib.sha256(os.urandom(1024)).hexdigest()

      tag_hmac = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
      tag_hmac.update(bytearray(uid)) # note - in its original binary glory and order.
      tag_encoded = tag_hmac.hexdigest()

      data = "open " + machine+ " " + tag_encoded
   
      HMAC = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
      HMAC.update(data.encode('ASCII'))
      hexdigest = HMAC.hexdigest()
   
      data = "SIG/1.00 " + hexdigest + " " + nonce + " " + node + " " + data

      logging.debug("Sending @"+topic+": "+data)
      publish.single(topic, data, hostname=cnf['mqtt']['host'], protocol="publish.MQTTv311")

# Capture SIGINT for cleanup when the script is aborted
def end_read(signal,frame):
    global forever
    logging.info("Ctrl+C captured, aborting.")
    forever= False

signal.signal(signal.SIGINT, end_read)
signal.signal(signal.SIGQUIT, end_read)

def on_connect(client, userdata, flags, rc):
    logging.info(("(re)Connected with result code "+str(rc)))
    topic = cnf['mqtt']['sub']+"/" + cnf['node'] + "/reply"

    if sys.version_info[0] < 3:
       topic = topic.encode('ASCII')

    mid = client.subscribe(topic)
    logging.debug(("Subscription req to {0} with MID={1}".format(cnf['mqtt']['sub'], mid)))

def on_subscribe(client, userdata, mid, granted_qos):
    logging.info(("(re)Subscribed confirmed for {0}".format(mid)))

def on_message(client, userdata, message):
    logging.debug(("Payload: %s",message.payload))

    payload = None
    try:
      payload = message.payload.decode('ASCII')
    except:
      logging.info("Non ascii equest '{0}' -- ignored".format(message.payload))
      return

    topic = message.topic

    if payload.startswith("SIG/"):
      try:
        hdr, sig, payload = payload.split(' ',2)
      except:
        logging.info("Could not parse '{0}' -- ignored".format(payload))
        raise
        return

      if not 'secret' in cnf:
        logging.critical("No secret configured for this node.")
        return

      secret = cnf['secret']
      global nonce
     
      HMAC = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
      HMAC.update(topic.encode('ASCII'))
      HMAC.update(payload.encode('ASCII'))
      hexdigest = HMAC.hexdigest()

      if not hexdigest == sig:
        logging.warning("Invalid signatured; ignored.")
        return

    try:
      what, which, result = payload.split()
    except:
      logging.warning("Cannot parse payload; ignored")
      return

    if which != machine:
      logging.info("I am a '{0}'- ignoring '{1}; ignored".format(which,machine))
      return

    if what != 'open':
      logging.warning("Unexpected command '{0}' - I can only <open> the <{1}>; ignored".format(what,machine))
      return

    if result == 'denied':
      logging.info("Denied XS")
      return

    if result != 'approved':
      logging.info("Unexpected result; ignored")
      return

    open_door()

client = mqtt.Client()

if not cnf['mqtt']['host'] or not cnf['mqtt']['sub']:
  logging.critical("No MQTT configured. aborting.")
  os.exit(1)

client.connect(cnf['mqtt']['host'])
client.on_message = on_message
client.on_connect = on_connect
client.on_subscribe= on_subscribe


while forever:
   client.loop()
   if args.C:
     for tag in args.C:
       if sys.version_info[0] < 3:
         tag_asbytes= ''.join(chr(int(x)) for x in tag.split("-"))
       else:
         tag_asbytes = bytearray(map(int,tag.split("-")))
       send_request(tag_asbytes)
     # we only do this once.
     args.C = None

   uid = None
   if args.offline:
     (status,TagType) = (None, None)
   else:
     (status,TagType) = MIFAREReader.MFRC522_Request(MIFAREReader.PICC_REQIDL)
     if status == MIFAREReader.MI_OK:
        (status,uid) = MIFAREReader.MFRC522_Anticoll()
        if status == MIFAREReader.MI_OK:
          logging.info("Swiped card "+'-'.join(map(str,uid)))
        else:
          uid = None
     
   if last_tag != uid:
      localtime = time.asctime( time.localtime(time.time()) )
      logging.info(localtime + "     Card UID: "+'-'.join(map(str,uid)))
      send_request(uid)
      last_tag = uid

client.disconnect()

if not args.offline:
  GPIO.cleanup()

sys.exit(0)
