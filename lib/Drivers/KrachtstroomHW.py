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

# We're careful as to not import the 
# RPIO itself; as it magically claims
# pin 22; thus conflicting with MFRC522.
#
import RPIO.PWM


class KrachtstroomHW(GPIOACNode):

  topLedTransitionsPerCycle = 0
  bottomLedTransitionsPerCycle = 0

  def setup(self):
    super().setup()

    if self.cnf.offline:
      self.logger.info("TEST: import RPIO.PWM")
      self.logger.info("TEST: initializing RPIO.PWM")
      return

    RPIO.PWM.set_loglevel(RPIO.PWM.LOG_LEVEL_ERRORS)
    RPIO.PWM.setup(pulseInc)
    RPIO.PWM.init_channel(relayChannel, subcycle_time_us=int(1e6/frequency))
    # Cycle time in microSeconds == 1 second
    RPIO.PWM.init_channel(ledChannel, subcycle_time_us=int(1e6/1)) 

    # Flash top LED while we get our bearings.
    #
    self.setTopLED(20)

  def cleardownGPIO(self):
   self.power(0)
   self.setBottomLED(0)
   self.setTopLED(0)

   RPIO.PWM.clear_channel_gpio(relayChannel,relay)
   RPIO.PWM.clear_channel_gpio(ledChannel,topLed)
   RPIO.PWM.clear_channel_gpio(ledChannel,bottomLed)

   # Shutdown all RPIO.PWM and DMA activity
   RPIO.PWM.cleanup()


  def setLEDs(self):

    if self.cnf.offline:
      self.logger.info("TEST: topled=%d bottomled=%d", self.topLedTransitionsPerCycle, self.bottomLedTransitionsPerCycle)
      return

    RPIO.PWM.clear_channel(ledChannel)
    for pin, state in { topLed: self.topLedTransitionsPerCycle, bottomLed: self.bottomLedTransitionsPerCycle }.items():
      if state:
        ds = ledFull / (state*2 - 1)
        for i in range(0,state):
          RPIO.PWM.add_channel_pulse(ledChannel, pin, start=int(i*ds*2), width=int(ds))
      else:
        RPIO.PWM.add_channel_pulse(ledChannel, pin, start=0, width=1)

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
        RPIO.PWM.clear_channel(relayChannel)
        RPIO.PWM.add_channel_pulse(relayChannel, relay, start=0, width=relayFull)
        time.sleep(holdDelay)
        RPIO.PWM.clear_channel(relayChannel)
        RPIO.PWM.add_channel_pulse(relayChannel, relay, start=0, width=relayLow)
    else:
        RPIO.PWM.add_channel_pulse(relayChannel, relay, start=0, width=0)
        RPIO.PWM.clear_channel(relayChannel)

  def on_exit(self,exitcode):
    if not self.cnf.offline:
      self.cleardownGPIO()
    else:
      self.logger.debug("TEST: GPIO_cleanup() called.")

    super().on_exit(exitcode)

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


