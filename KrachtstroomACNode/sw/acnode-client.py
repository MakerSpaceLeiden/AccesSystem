#!/usr/bin/env python

import time
import json

import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish

import RPIO 
from RPIO import PWM

import MFRC522

import sys
import time
import signal

import time
import logging

sys.path.append('../../lib')
import configRead
import alertEmail

configRead.load('config.json')
cnf = configRead.cnf

time.sleep(5)

# GPIO Wiring
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

ledChannel = 1
relayChannel = 0

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
  # global topLedTransitionsPerCycle, bottomLedTransitionsPerCycle

  PWM.clear_channel(ledChannel)
  for pin, state in { topLed: topLedTransitionsPerCycle, bottomLed: bottomLedTransitionsPerCycle }.iteritems():
    if state:
      ds = ledFull / (state*2 - 1)
      for i in range(0,state):
        PWM.add_channel_pulse(ledChannel, pin, start=i*ds*2, width=ds)
  
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
user = None
machine = None
machine_tag = None
last_ok = 0
uname = None

grace = 2
if 'grace' in cnf:
   grace = cnf['grace']

forever= True

# Capture SIGINT for cleanup when the script is aborted
def end_read(signal,frame):
    global forever 
    print "Ctrl+C captured, aborting."
    forever= False

signal.signal(signal.SIGINT, end_read)
MIFAREReader = MFRC522.MFRC522()

while forever:
   (status,TagType) = MIFAREReader.MFRC522_Request(MIFAREReader.PICC_REQIDL)
   uid = None

   if status == MIFAREReader.MI_OK:
        (status,uid) = MIFAREReader.MFRC522_Anticoll()
        if status == MIFAREReader.MI_OK:
           localtime = time.asctime( time.localtime(time.time()) )
           print localtime + "	Card UID: "+'-'.join(map(str,uid))
        else:
           print "Error"

   if powered:
       # check that the right plug tag is still being read.
       #
       if machine_tag == uid:
          print "plug tag still detected."
          last_ok = time.time()

       if time.time() - last_ok > grace:
          print "Power down."
          powered = 0
          uname = None
          machine = None
          PWM.add_channel_pulse(relayChannel, relay, start=0, width=0)
          setBottomLED(0)
   else:
       # we are not powered - so waiting for a user tag or device tag.
       #
       if uid:
         u = find_usertag(uid)
         if u:
           uname = u
           print "User " + uname +" swiped and known."
           setBottomLED(4)
           last_ok = time.time()

         m = find_machinetag(uid)
         if m:
           if uname:
              machine = m
              print "Machine " + machine + " now wired up."
           else:
              print "Ignoring machine tag without user tag."
              setBottomLED(8)
              last_ok = time.time() + grace - 0.5
              machine = None
         
       if uname and machine:
          print "Both fine. Powering up the " + machine
          setBottomLED(1)

          PWM.add_channel_pulse(relayChannel, relay, start=0, width=relayFull)
          time.sleep(holdDelay)
          PWM.add_channel_pulse(relayChannel, relay, start=0, width=relayLow)

          machine_tag = cnf['machines'][machine]['tag']
          powered = 1

       if time.time() - last_ok > grace:
          setBottomLED(0)
 

# Needed to clear down the GPIO back to input (cleanup() does not do that).
#
PWM.clear_channel_gpio(relayChannel,relay)
PWM.clear_channel_gpio(ledChannel,topLed)
PWM.clear_channel_gpio(ledChannel,bottomLed)

# Shutdown all PWM and DMA activity
PWM.cleanup()

# GPIO.cleanup()
