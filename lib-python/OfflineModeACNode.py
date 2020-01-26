#!/usr/bin/env python3.4
#
import time
import sys
import os

from ACNode import ACNode

class OfflineModeACNode(ACNode):
  # Firstly - we have an 'offline' mode that allows for
  # testing without the hardware (i.e. on any laptop or
  # machine with python); without the need for the stepper
  # motor, mosfet or RFID reader.
  #
  def parseArguments(self):
    self.parser.add('--offline', action='count',
                   help='Activate offline/no-hardware needed test mode; implies max-verbose mode.')
    super().parseArguments()

  # We load the hardware related libraries late and
  # on demand; this allows for an '--offline' flag.
  #
  def setup(self):
    super().setup()

    # Go very verbose if we are in fake hardware mode.
    #
    if self.cnf.offline:
       self.cnf.verbose = 10
