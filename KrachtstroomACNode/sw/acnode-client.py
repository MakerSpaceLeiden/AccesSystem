#!/usr/bin/env python

import time
import json

import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish

import RPi.GPIO as GPIO 
import MFRC522

import sys
import time
import signal

import time
import logging

sys.path.append('../../lib')
import configRead
import alertEmail

configRead.load()
cnf = configRead.cnf

# GPIO Wiring
topLed=16
bottomLed=18

fastFlashFrequency = 8
slowFlashFrequency = 2

relay=7

# Relay power reduction control
frequency=10000
holdpwm=5 # strangely - down to 1 percent duty cycle seems to be fine.
holdDelay=0.3 # seconds

alertEmail.send_email("Test", "Test na import", "dirkx@webweaving.org")

GPIO.setmode(GPIO.BOARD) 
for pin in topLed, bottomLed, relay:
  GPIO.setup(pin,GPIO.OUT) 
  GPIO.output(pin,False)

relayCtrl = GPIO.PWM(relay, frequency)
relayCtrl.start(0)

topLedCtrl = GPIO.PWM(topLed, slowFlashFrequency)
bottomLedCtrl= GPIO.PWM(bottomLed, slowFlashFrequency)

topLedCtrl.start(50)
bottomLedCtrl.start(0)

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
topLedCtrl.start(100)

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
    GPIO.cleanup()

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
          bottomLedCtrl.start(0)
          relayCtrl.start(0)

   else:
       # we are not powered - so waiting for a user tag or device tag.
       #
       if uid:
         u = find_usertag(uid)
         if u:
           uname = u
           print "User " + uname +" swiped and known."
           bottomLedCtrl.start(50)
           bottomLedCtrl.ChangeFrequency(slowFlashFrequency)
           last_ok = time.time()

         m = find_machinetag(uid)
         if m:
           if uname:
              machine = m
              print "Machine " + machine + " now wired up."
           else:
              print "Ignoring machine tag without user tag."
              bottomLedCtrl.start(50)
              bottomLedCtrl.ChangeFrequency(fastFlashFrequency)
              last_ok = time.time() + grace - 0.5
              machine = None
         
       if uname and machine:
           print "Both fine. Powering up the " + machine
           bottomLedCtrl.start(100)
           relayCtrl.start(100)
           time.sleep(holdDelay)
           relayCtrl.start(holdpwm)
           machine_tag = cnf['machines'][machine]['tag']
           powered = 1

       if time.time() - last_ok > grace:
           bottomLedCtrl.start(0)
 


