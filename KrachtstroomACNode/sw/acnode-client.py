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

import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish

# We're careful as to not import the 
# RPIO itself; as it magically claims
# pin 22; thus conflicting with MFRC522.
#
import RPIO.PWM as PWM

# Note: The current MFC522 library claims pin22/GPIO25
# as the reset pin -- set by the constant NRSTPD near
# the start of the file.
#
import MFRC522

logging.basicConfig(level=logging.DEBUG)

sys.path.append('../../lib')
import configRead
import alertEmail

configRead.load('config.json')
cnf = configRead.cnf

# GPIO Wiring
#
topLed=23	# GPIO23, Pin 16
bottomLed=24	# GPIO24, Pin 18
relay=4		# GPIO4 (pin 7)

fastFlashFrequency = 8
slowFlashFrequency = 2

# Relay power reduction control
frequency=100 	# Hz
holdpwm=25	# percent
holdDelay=0.3 	# seconds

pulseInc = 50 # in Micro Seconds

grace = 6	# seconds - timeout between card offer & cable offer.
graceOff = 2	# seconds -- timeout between removal of card and powerdown.

ledChannel = 1
relayChannel = 0

# No user maintainable parts beyond this line.
#
relayFull = int(1e6/frequency/pulseInc - 1)
relayLow = max(1,int(relayFull * holdpwm / 100 -1))

ledFull = int(1e6/1/pulseInc - 1)

PWM.set_loglevel(PWM.LOG_LEVEL_ERRORS)
PWM.setup(pulseInc)
PWM.init_channel(relayChannel, subcycle_time_us=int(1e6/frequency)) 
PWM.init_channel(ledChannel, subcycle_time_us=int(1e6/1)) # Cycle time in microSeconds == 1 second

topLedTransitionsPerCycle = 0
bottomLedTransitionsPerCycle = 0

def setLEDs():
  global topLedTransitionsPerCycle, bottomLedTransitionsPerCycle
  PWM.clear_channel(ledChannel)
  for pin, state in { topLed: topLedTransitionsPerCycle, bottomLed: bottomLedTransitionsPerCycle }.iteritems():
    if state:
      ds = ledFull / (state*2 - 1)
      for i in range(0,state):
        PWM.add_channel_pulse(ledChannel, pin, start=i*ds*2, width=ds)
    else:
        PWM.add_channel_pulse(ledChannel, pin, start=0, width=1)
  
def setTopLED( state ):
   global topLedTransitionsPerCycle
   topLedTransitionsPerCycle=state
   setLEDs()

def setBottomLED( state ):
   global bottomLedTransitionsPerCycle
   bottomLedTransitionsPerCycle=state
   setLEDs()

# Flash top LED while we get our bearings.
#
setTopLED(20)

def find_usertag(uid=None):
  try:
     idx = cnf['users'].values().index(uid)
     return cnf['users'].keys()[ idx ]
  except:
     return None

  return None

def find_machinetag(uid=None):
  m = cnf['machines']
  for key in m.keys():
    if m[key]['tag'] == uid:
       return key

  return None

# Ready to start - turn top LED full on.
setTopLED(1)

powered = 0
machine = None
machine_tag = None
last_ok = 0
user_tag = None
last_tag = None

if 'grace' in cnf:
   grace = cnf['grace']
if 'graceOff' in cnf:
   graceOff = cnf['graceOff']

forever= True

# Capture SIGINT for cleanup when the script is aborted
def end_read(signal,frame):
    global forever 
    print "Ctrl+C captured, aborting."
    forever= False

signal.signal(signal.SIGINT, end_read)
signal.signal(signal.SIGQUIT, end_read)

MIFAREReader = MFRC522.MFRC522()

def on_connect(client, userdata, flags, rc):
    print("(re)Connected with result code "+str(rc))
    topic = cnf['mqtt']['sub']+"/" + cnf['node'] + "/reply"

    if sys.version_info[0] < 3:
       topic = topic.encode('ASCII')

    mid = client.subscribe(topic)
    print("Subscription req to {0} with MID={1}".format(cnf['mqtt']['sub'], mid))

def on_subscribe(client, userdata, mid, granted_qos):
    print("(re)Subscribed confirmed for {0}".format(mid))


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
	# 'SIG/1.00 27b1f...498c40d1cb85173de8e6026604bea234107d energize circelzaag approved' 
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
      logging.info("Cannot parse payload; ignored")
      return
 
    global user_tag, machine

    if machine != which:
      logging.info("Unexpected machine; ignored")
      return

    if what != 'energize':
      logging.info("Unexpected command; ignored")
      return

    if result == 'denied':
      logging.info("Denied XS")
      setBottomLED(5)
      return

    if result != 'approved':
      logging.info("Unexpected result; ignored")
      return


    if not user_tag or not machine:
      logging.info("Lost my mind - ignored the ok.")
      return
 
    print "Got the OK - Powering up the " + machine
    setBottomLED(1)

    PWM.clear_channel(relayChannel)
    PWM.add_channel_pulse(relayChannel, relay, start=0, width=relayFull)
    time.sleep(holdDelay)

    PWM.clear_channel(relayChannel)
    PWM.add_channel_pulse(relayChannel, relay, start=0, width=relayLow)

    global machine_tag, powered, last_ok
    machine_tag = cnf['machines'][machine]['tag']
    powered = 1
    last_ok = time.time()

if not 'secret' in cnf:
    logger.critical("No secret for this node defined. aborting.")
    sys.exit(1)

if not 'node' in cnf:
    logger.critical("No node name defined. aborting.")
    sys.exit(1)

client = mqtt.Client()

client.connect(cnf['mqtt']['host'])
client.on_message = on_message
client.on_connect = on_connect
client.on_subscribe= on_subscribe

master = 'master'
if 'master' in cnf:
  master = cnf['masternode']

topic = cnf['mqtt']['sub'] + "/" + master + "/" + cnf['node']

while forever:
   client.loop()

   # We are doing this reading 'in' the main loop. This causes two
   # issues. Firstly it is rather slow; and hence responding to
   # various events and timeouts can be 0.5-1 second late.
   # Secondly; once a card is contienously present; we get the
   # occasional 'no card'; so we should not instantly trigger
   # on this; and again allow some grace. This makes detecting
   # the removal of a card slower.
   #
   (status,TagType) = MIFAREReader.MFRC522_Request(MIFAREReader.PICC_REQIDL)
   uid = None

   if status == MIFAREReader.MI_OK:
        (status,uid) = MIFAREReader.MFRC522_Anticoll()
        if status == MIFAREReader.MI_OK:
           localtime = time.asctime( time.localtime(time.time()) )
           if last_tag != uid:
              print localtime + "	Card UID: "+'-'.join(map(str,uid))
           last_tag = uid
        else:
           print "Error"

   if powered:
       # check that the right plug tag is still being read.
       #
       if machine_tag == uid:
          # print "plug tag still detected."
          last_ok = time.time()

       if time.time() - last_ok > graceOff:
          print "Power down."
          powered = 0
          user_tag = None
          machine = None
          PWM.add_channel_pulse(relayChannel, relay, start=0, width=0)
          PWM.clear_channel(relayChannel)
          setBottomLED(0)
   else:
       # we are not powered - so waiting for a user tag or device tag.
       #
       if uid:
         m = find_machinetag(uid)
         if not m:
           print "Assuming this is a user tag."
           user_tag = uid
           setBottomLED(4)

           # Allow the time to move along if the user holds the
           # card long against the reader. So in effect the grace
           # period becomes the time between cards.
           #
           last_ok = time.time()

         if m:
           if user_tag:
             machine = m
             print "Machine " + machine + " now wired up - requesting permission"

             secret = cnf['secret']
             nonce = hashlib.sha256(os.urandom(1024)).hexdigest()

             tag_hmac = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
             tag_hmac.update(bytearray(user_tag)) # note - in its original binary glory and order.
             tag_encoded = tag_hmac.hexdigest()

             data = "energize " + machine + " " + tag_encoded

             HMAC = hmac.new(secret.encode('ASCII'),nonce.encode('ASCII'),hashlib.sha256)
             HMAC.update(data.encode('ASCII'))
             hexdigest = HMAC.hexdigest()

             data = "SIG/1.00 " + hexdigest + " " + nonce + " " + cnf['node'] + " " + data

             publish.single(topic, data, hostname=cnf['mqtt']['host'], protocol="publish.MQTTv311")
             setBottomLED(50)
           else:
              print "Ignoring machine tag without user tag."

              # flash the led for about a third of the grace time.
              #
              setBottomLED(8)
              last_ok = time.time() + grace - grace/3
              machine = None
         
       if time.time() - last_ok > grace:
          setBottomLED(0)
 

# Needed to clear down the GPIO back to input (cleanup() does not do that).
#
PWM.clear_channel_gpio(relayChannel,relay)
PWM.clear_channel_gpio(ledChannel,topLed)
PWM.clear_channel_gpio(ledChannel,bottomLed)

# Shutdown all PWM and DMA activity
PWM.cleanup()

client.disconnect()

