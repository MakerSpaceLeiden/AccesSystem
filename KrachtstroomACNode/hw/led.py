import RPi.GPIO as GPIO 
import time

# Top and bottom LEDs
# - Flash alternating.
#

GPIO.setmode(GPIO.BOARD) 

for pin in 16,18:
  print "Now flashing Pin " + str(pin)

  GPIO.setup(pin, GPIO.OUT) 

  for cnt in range(1,5):
    print "."
    GPIO.output(pin,True) 
    time.sleep(0.5)
    GPIO.output(pin,False)
    time.sleep(0.5)


