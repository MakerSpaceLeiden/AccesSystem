import RPi.GPIO as GPIO 
import time

# Test script for the relay; powers it up
# at full PWM for a second; drops the voltage
# to about 10 volts (to reduce heat generation
# in the 330 ohm resistor) for 5 seconds; and 
# then switches # it off for a second.

pin=7
frequency=5000
holdpwm=10 # down to 2 seems to be reliable.

GPIO.setmode(GPIO.BOARD) 
GPIO.setup(pin, GPIO.OUT) 

p = GPIO.PWM(pin, frequency)

while True:
  print "Full on"
  p.start(100)
  time.sleep(0.3)

  print "Dropping back"
  p.start(holdpwm)
  time.sleep(5)

  print "off"
  p.stop()
  time.sleep(1)

