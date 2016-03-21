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

logging.basicConfig(filename= node + '.log',level=logging.DEBUG)
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

def reply(topic, data, nonce = None):
   secret = None

   path = topic.split('/')
   what = path[-1]
   who = path[-2]

   if who in cnf['secrets']:
     secret = cnf['secrets'][who]
   else:
     print("Legayc no sec "+who)


   if data != "open" and secret and nonce:
     HMAC = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
     payload = json.dumps(data)
     HMAC.update(payload.encode('ASCII'))
     hexdigest = HMAC.hexdigest()
 
     data = "{ "
     data +="\"HMAC-SHA256\" : \"" + hexdigest + "\","
     data +="\"payload\" : " + payload 
     data += "}"
     
   publish.single(topic, data, hostname=cnf['mqtt']['host'], protocol="publish.MQTTv311")

def on_message(client, userdata, message):
    print("Payload: %s",message.payload)

    payload = None
    try:
      payload = message.payload.decode('ASCII')
    except:
      logging.info("Non ascii equest '%s' -- ignored", message.payload)
      reply(topic, "malformed request", nonce)
      return

    topic = message.topic

    path = topic.split('/')
    what = path[-1]
    who = path[-2]
     
    command, tag, nonce = ( payload.split() + [None]*10)[:3]

    if not command == 'request':
       return

    if not tag:
       logging.info("Malformed request '%s'", payload)
       reply(topic, "malformed request", nonce)
       return

    if not tag in userdb:
       logging.info("Unknown tag '%s' action: '%s'", tag, topic);
       reply(topic, "unknown", nonce)
       return

    email = userdb[tag]['email'];
    name = userdb[tag]['name'];

    if not who in userdb[tag]['access']:
       logging.info("tag '%s' (%s) denied action: '%s' on '%s'", tag, name, what, who);
       reply(topic, "denied", nonce)
       return

    logging.info("tag '%s' (%s) OK for action: '%s' on '%s'", tag, name, what, who);
    reply(topic, "ok", nonce)

def on_connect(client, userdata, flags, rc):
    print("(re)Connected with result code "+str(rc))
    topic = cnf['mqtt']['sub']+"/#"

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
   topic = cnf['mqtt']['sub']

   data = {
	'machine' : machine,
	'tag' : tag,
	'operation' : 'energize'
   }
   data = json.dumps(data)

   if machine in cnf['secrets']:
     secret = cnf['secrets'][machine]
     nonce = hashlib.sha256(os.urandom(1024)).hexdigest()
     payload = data

     HMAC = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
     HMAC.update(payload.encode('ASCII'))
     hexdigest = HMAC.hexdigest()

     data = "{" 
     data +=   "\"HMAC-SHA256\" : \"" + hexdigest + "\"," 
     data +=   "\"nonce\" : \"" + nonce + "\"," 
     data +=   "\"payload\" : " + payload 
     data += "}"

   else:
     print("Legayc no secret defined for "+machine)
     
   publish.single(topic, data, hostname=cnf['mqtt']['host'], protocol="publish.MQTTv311")
else:
   while continueForever:
     client.loop()

client.disconnect()
