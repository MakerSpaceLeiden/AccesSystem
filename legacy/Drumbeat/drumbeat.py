#!/usr/bin/env python3.4

import os
import sys
import time
import hmac
import hashlib


sys.path.append('../lib')
import DrumbeatNode as DrumbeatNode

drumbeat = DrumbeatNode.DrumbeatNode()

if not drumbeat:
    sys.exit(1)

sys.exit(drumbeat.run())

