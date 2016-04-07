#!/usr/bin/env python3.4
#
from SensorACNode import SensorACNode
from ActuatorACNode import ActuatorACNode

class SimpleACNode(SensorACNode, ActuatorACNode):
  # glue - though negates the need for rolling.
  nonce = None

