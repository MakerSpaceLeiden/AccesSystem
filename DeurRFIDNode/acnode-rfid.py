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
else:
  # Note: The current MFC522 library claims pin22/GPIO25
  # as the reset pin -- set by the constant NRSTPD near
  # the start of the file.
  #
  import MFRC522
  MIFAREReader = MFRC522.MFRC522()

last_tag = None

forever= True

# Capture SIGINT for cleanup when the script is aborted
def end_read(signal,frame):
    global forever 
    logging.info("Ctrl+C captured, aborting.")
    forever= False

signal.signal(signal.SIGINT, end_read)
signal.signal(signal.SIGQUIT, end_read)

if not 'secret' in cnf:
    logging.critical("No secret for this node defined. aborting.")
    sys.exit(1)
secret = cnf['secret']

if not 'node' in cnf:
    logging.critical("No node name defined. aborting.")
    sys.exit(1)
node = cnf['node']

machine = 'deur'
controller = 'deur'

if 'machine' in cnf:
  machine = cnf['machine']

if 'controller' in cnf:
  controller = cnf['controller']

master = 'master'
if 'master' in cnf:
  master = cnf['masternode']

topic = cnf['mqtt']['sub']+"/" + master + "/" + node

def send_request(uid = None):
      nonce = hashlib.sha256(os.urandom(1024)).hexdigest()

      tag_hmac = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
      tag_hmac.update(bytearray(uid)) # note - in its original binary glory and order.
      tag_encoded = tag_hmac.hexdigest()

      data = "open " + machine+ " " + tag_encoded
   
      HMAC = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
      HMAC.update(data.encode('ASCII'))
      hexdigest = HMAC.hexdigest()
   
      data = "SIG/1.00 " + hexdigest + " " + nonce + " " + controller + " " + data

      logging.debug("Sending @"+topic+": "+data)
      publish.single(topic, data, hostname=cnf['mqtt']['host'], protocol="publish.MQTTv311")

if args.C:
  for tag in args.C:
    tag_asbytes= ''.join(chr(int(x)) for x in tag.split("-") )
    send_request(tag_asbytes)
  sys.exit(0)

while forever:
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
      logging.info(localtime + "	Card UID: "+'-'.join(map(str,uid)))
      send_request(uid)


      last_tag = uid

sys.exit(0)
sys.exit(0)
