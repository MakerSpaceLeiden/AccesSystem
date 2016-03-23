#!/usr/bin/env python

import os
import sys
import time
import signal

import logging
import string
import hmac
import hashlib
import json
import argparse

import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish

sys.path.append('../lib')
import configRead
import alertEmail

parser = argparse.ArgumentParser(description='ACNode Master server.')
parser.add_argument('-v', action='count',
                   help='Verbose')
parser.add_argument('-c', type=argparse.FileType('r'), metavar='file.json',
                   help='Configuration file')
parser.add_argument('-s', type=str, metavar='hostname',
                   help='MQTT Server (FQDN or IP address)')
parser.add_argument('-C', nargs=2, metavar=('tag', 'machine'),
                   help='Client mode - useful for testing')

args = parser.parse_args()

configfile = None
if args.c:
  configfile = args.c

configRead.load(configfile)
cnf = configRead.cnf

node=cnf['node']
mailsubject="-"

# logging.basicConfig(filename= node + '.log',level=logging.DEBUG)
logging.basicConfig(level=logging.DEBUG)

logger = logging.getLogger() 

ACCESS_UKNOWN, ACCESS_DENIED, ACCESS_GRANTED = ('UNKNOWN', 'DENIED', 'GRANTED')

client = mqtt.Client()

def parse_db(dbfile):
     userdb = {}
     try:
       for row in open(dbfile,'r'):
          if row.startswith('#'): continue
          if len(row.split(':')) != 4: continue
          
          # Parse keys to valid input
          tag,access,name,email = row.strip().split(':')
          tag = '-'.join(map(str.strip, tag.split(',')))
          
          # Break access up into the things this user
          # has access to.          
          allowed_items = access.split(',')

          # print("-- "+tag+" - "+name+"/"+access)

          # Create database
          userdb[tag] = { 'tag': 'hidden', 'access': allowed_items, 'name': name, 'email': email }
     except IOError as e:
       print("I/O error %s", e.strerror())
       raise
     except ValueError:
       print("Could not convert data to an integer -- some malformed tag ?")
       raise
     except:
       print("Unexpected error: %s", sys.exc_info()[0])
       raise

     return userdb

'''
-       MQTT reply from master to (relevant) ACNode(s)

        topic:          PREFIX/acnode/<node>/reply

        payload:        bytes interpreted as 7-bit safe ASCII

        'SIG/1.00'      protocol version.
        <space>
        hexdigest       SHA256 based HMAC of reply-topic, request-nonce, secret and message
        <space>
        message         bytes; until the remainder of the message

        Possible replies

        'energize' <space> <devicename> <space> 'approved'
        'energize' <space> <devicename> <space> 'denied'
        'energize' <space> <devicename> <space> 'error'
'''

def reply(replytopic, data, requestnonce = None):
   secret = None

   try:
     path = replytopic.split('/')
     what = path[-1]
     which = path[-2]
   except:
     logging.error("Cannot parse replytopic '{0}' -- ignoring".format(replytopic))
     return

   if which in cnf['secrets']:
     secret = cnf['secrets'][which]
   else:
     print("No secret defined to reply with - answering in the clear")

   if secret and requestnonce:
     HMAC = hmac.new(secret.encode('ASCII'),requestnonce.encode('ASCII'),hashlib.sha256)
     HMAC.update(replytopic.encode('ASCII'))
     HMAC.update(data.encode('ASCII'))
     hexdigest = HMAC.hexdigest()
 
     data = 'SIG/1.00 ' + hexdigest + ' ' + data.encode('ASCII')
   else:
     if data != 'open':
       print("Legacy mode - can only reply with <open>")
       return
     
   publish.single(replytopic, data, hostname=cnf['mqtt']['host'], protocol="publish.MQTTv311")

def on_message(client, userdata, message):
    print("Payload: %s",message.payload)

    payload = None
    try:
      payload = message.payload.decode('ASCII')
    except:
      logging.info("Non ascii equest '{0}' -- ignored".format(message.payload))
      return

    topic = message.topic

    if payload.startswith("SIG/"):
      try:
        hdr, sig, nonce, node, payload = payload.split(' ',4)
      except:
        logging.info("Could not parse '{0}' -- ignored".format(payload))
        return

      if not node in cnf['secrets']:
        logging.critical("No secret known for node '{0}' -- ignored.".format(node))
        return

      secret = cnf['secrets'][node]
      HMAC = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
      HMAC.update(payload.encode('ASCII'))
      hexdigest = HMAC.hexdigest()

      if not hexdigest == sig:
        logging.info("Invalid signatured; ignored.")
        return

    try:
      what, which, tag_encoded = payload.split()
    except:
      logging.info("Cannot parse payload; ignored")
      return

    tag = None
    for uid in userdb.keys():
      tag_hmac = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
      try:
        tag_asbytes= ''.join(chr(int(x)) for x in uid.split("-") )
      except:
        logging.error("Could not parse tag '{0}' in config file-- skipped".format(uid))
        continue

      tag_hmac.update(tag_asbytes)
      tag_db = tag_hmac.hexdigest()

      if tag_encoded == tag_db:
         tag = uid
         break

    if not tag in userdb:
      logging.info("Unknown tag; ignored")
      return

    if not tag in userdb:
      logging.info("Tag not in DB; ignored")
      return

    email = userdb[tag]['email'];
    name = userdb[tag]['name'];

    msg = 'energize ' + which + ' ';
    if not which in userdb[tag]['access']:
       logging.info("tag '%s' (%s) denied action: '%s' on '%s'", tag, name, what, which);
       msg += 'denied'
    else:
       logging.info("tag '%s' (%s) OK for action: '%s' on '%s'", tag, name, what, which);
       msg += 'approved'

    print("Response: "+msg)

    reply(cnf['mqtt']['sub']+'/'+node+'/reply' ,msg, nonce)

def on_connect(client, userdata, flags, rc):
    print("(re)Connected with result code "+str(rc))
    topic = cnf['mqtt']['sub']+"/master"

    if sys.version_info[0] < 3:
       topic = topic.encode('ASCII')

    mid = client.subscribe(topic)
    print("Subscription req to {0} with MID={1}".format(cnf['mqtt']['sub'], mid))

def on_subscribe(client, userdata, mid, granted_qos):
    print("(re)Subscribed confirmed for {0}".format(mid))

continueForever = True
def end_loop(signal,frame):
    global continueForever
    print("Ctrl+C captured, ending subscribe, etc")
    continueForever = False

signal.signal(signal.SIGINT, end_loop)
signal.signal(signal.SIGQUIT, end_loop)

client.connect(cnf['mqtt']['host'])
client.on_message = on_message
client.on_connect = on_connect
client.on_subscribe= on_subscribe

userdb = parse_db(cnf['dbfile'])

if args.C:
   print("Client mode - one post shot.")
   tag,machine = args.C
   topic = cnf['mqtt']['sub'] + '/master'

   data = 'energize ' + machine + ' ' + tag

   if machine in cnf['secrets']:
     secret = cnf['secrets'][machine]
     nonce = hashlib.sha256(os.urandom(1024)).hexdigest()
     payload = data

     HMAC = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
     HMAC.update(payload.encode('ASCII'))
     hexdigest = HMAC.hexdigest()

     data = "SIG/1.00 " + hexdigest + " " + nonce + " " + machine + " " + payload
   else:
     print("Legayc no secret defined for "+machine)
     
   publish.single(topic, data, hostname=cnf['mqtt']['host'], protocol="publish.MQTTv311")
else:
   while continueForever:
     client.loop()

client.disconnect()
