#!/usr/bin/env python3.4
#
import time 
import sys
import os

# GPIO Wiring
#
topLed=23       # GPIO23, Pin 16
bottomLed=24    # GPIO24, Pin 18
relay=4         # GPIO4 (pin 7)

fastFlashFrequency = 8
slowFlashFrequency = 2

# Relay power reduction control
frequency=100   # Hz
holdpwm=25      # percent
holdDelay=0.3   # seconds

pulseInc = 50 # in Micro Seconds

ledChannel = 1
relayChannel = 0

relayFull = int(1e6/frequency/pulseInc - 1)
relayLow = max(1,int(relayFull * holdpwm / 100 -1))

ledFull = int(1e6/1/pulseInc - 1)

sys.path.append('../../lib')
from GPIOACNode import GPIOACNode

class KrachtstroomHW(GPIOACNode):

  topLedTransitionsPerCycle = 0
  bottomLedTransitionsPerCycle = 0

  def setup(self):
    super().setup()

    if self.cnf.offline:
      self.logger.info("TEST: import PWM")
      self.logger.info("TEST: initializing PWM")
      return

    # We're careful as to not import the 
    # RPIO itself; as it magically claims
    # pin 22; thus conflicting with MFRC522.
    #
    import RPIO.PWM as PWM

    PWM.set_loglevel(PWM.LOG_LEVEL_ERRORS)
    PWM.setup(pulseInc)
    PWM.init_channel(relayChannel, subcycle_time_us=int(1e6/frequency))
    # Cycle time in microSeconds == 1 second
    PWM.init_channel(ledChannel, subcycle_time_us=int(1e6/1)) 

    # Flash top LED while we get our bearings.
    #
    self.setTopLED(20)

  def cleardownGPIO(self):
   self.power(0)
   self.setBottomLED(0)
   self.setTopLED(0)

   PWM.clear_channel_gpio(relayChannel,relay)
   PWM.clear_channel_gpio(ledChannel,topLed)
   PWM.clear_channel_gpio(ledChannel,bottomLed)

   # Shutdown all PWM and DMA activity
   PWM.cleanup()


  def setLEDs(self):

    if self.cnf.offline:
      self.logger.info("TEST: topled=%d bottomled=%d", self.topLedTransitionsPerCycle, self.bottomLedTransitionsPerCycle)
      return

    PWM.clear_channel(ledChannel)
    for pin, state in { topLed: self.topLedTransitionsPerCycle, bottomLed: self.bottomLedTransitionsPerCycle }.iteritems():
      if state:
        ds = ledFull / (state*2 - 1)
        for i in range(0,state):
          PWM.add_channel_pulse(ledChannel, pin, start=i*ds*2, width=ds)
      else:
        PWM.add_channel_pulse(ledChannel, pin, start=0, width=1)

  def setTopLED(self, state ):
   self.topLedTransitionsPerCycle=state
   self.setLEDs()

  def setBottomLED(self, state ):
   self.bottomLedTransitionsPerCycle=state
   self.setLEDs()

  def power(self,state):
    if self.cnf.offline:
        self.logger.info("TEST: setting power=%d", state)
        return

    if state:
        PWM.clear_channel(relayChannel)
        PWM.add_channel_pulse(relayChannel, relay, start=0, width=relayFull)
        time.sleep(holdDelay)
        PWM.clear_channel(relayChannel)
        PWM.add_channel_pulse(relayChannel, relay, start=0, width=relayLow)
    else:
        PWM.add_channel_pulse(relayChannel, relay, start=0, width=0)
        PWM.clear_channel(relayChannel)




if __name__ == "__main__":
  acnode = KrachtstroomHW()
  acnode.initialize()

  print("Flashing top LED for 1 second")
  acnode.setTopLED(50)
  time.sleep(1)
  acnode.setTopLED(0)

  print("Flashing bottom LED for 1 second")
  acnode.setBottomLED(50)
  time.sleep(1)
  acnode.setTopLED(0)

  print("Activating relay for 1 second")
  acnode.power(1)
  time.sleep(1)
  acnode.power(0)
  print("Done.")

  sys.exit(0)


